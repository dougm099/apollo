#include "DnaLibrary.h"
#include <algorithm> // std::sort
#include <cmath>     // std::sqrt

//==============================================================================
DnaLibrary::DnaLibrary() = default;

juce::File DnaLibrary::getDemoFolder()
{
    const char* envDir = std::getenv ("APOLLO_DEMO_DIR");
    if (envDir != nullptr && envDir[0] != '\0')
    {
        juce::File f (envDir);
        if (f.isDirectory())
            return f;
    }

    auto f = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                .getChildFile ("Apollo")
                .getChildFile ("DemoLibrary");
    if (f.isDirectory())
        return f;

    auto homeMusic = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Music")
                        .getChildFile ("Apollo")
                        .getChildFile ("DemoLibrary");
    if (homeMusic.isDirectory())
        return homeMusic;

    return f;
}

void DnaLibrary::scanAndAnalyze (DnaExtractor& extractor)
{
    if (ready.load (std::memory_order_relaxed))
        return;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const juce::File folder = getDemoFolder();
    juce::Array<juce::File> wavs;
    if (folder.isDirectory())
        folder.findChildFiles (wavs, juce::File::findFiles, false, "*.wav");

    // Energy-role tags (v2.4): roles.txt maps "file.wav=role", one per line.
    // Closed vocabulary — see tools/DEMO_LIBRARY.md. Unknown/missing -> "neutral".
    juce::StringPairArray roleMap;
    const juce::File roleFile = folder.getChildFile ("roles.txt");
    if (roleFile.existsAsFile())
    {
        juce::StringArray lines;
        lines.addLines (roleFile.loadFileAsString());
        for (const auto& line : lines)
        {
            auto kv = juce::StringArray::fromTokens (line, "=", "");
            if (kv.size() == 2 && kv[0].trim().isNotEmpty())
                roleMap.set (kv[0].trim(), kv[1].trim().toLowerCase());
        }
    }

    // Sort by name for a stable list.
    std::sort (wavs.begin(), wavs.end(),
               [] (const juce::File& a, const juce::File& b)
               { return a.getFileName() < b.getFileName(); });

    std::vector<Entry> found;

    for (const auto& f : wavs)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr)
            continue;

        const int64 total = reader->lengthInSamples;
        if (total <= 0 || total > 60 * 44100 * 2) // sanity: skip absurd files
            continue;

        const int numSamples = (int) total; // capped above: fits in int
        std::vector<float> mono ((size_t) numSamples, 0.0f);
        const int numCh = (int) juce::jmin<int64> (reader->numChannels, 2);

        if (numCh == 1)
        {
            float* dst = mono.data();
            reader->read (&dst, 1, 0, numSamples);
        }
        else
        {
            std::vector<float> left ((size_t) numSamples), right ((size_t) numSamples);
            float* chans[2] = { left.data(), right.data() };
            reader->read (chans, 2, 0, numSamples);
            for (size_t i = 0; i < mono.size(); ++i)
                mono[i] = 0.5f * (left[i] + right[i]);
        }

        DnaFingerprint dna;
        if (extractor.extractDna (mono.data(), numSamples,
                                  reader->sampleRate, dna)
            && dna.valid)
        {
            Entry e;
            e.name = f.getFileNameWithoutExtension().replace ("-", " ");
            e.dna  = dna;
            e.role = roleMap.getValue (f.getFileName(), "neutral");
            found.push_back (e);
        }
    }

    {
        juce::SpinLock::ScopedLockType sl (lock);
        entries = std::move (found);
        rankOrder.resize (entries.size());
        for (size_t i = 0; i < rankOrder.size(); ++i)
            rankOrder[i] = i;
    }

    ready.store (true, std::memory_order_relaxed);
}

int DnaLibrary::size() const
{
    juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (lock));
    return (int) entries.size();
}

float DnaLibrary::weightedCosine (const std::array<float, 3>& a,
                                  const std::array<float, 3>& b,
                                  const float w[3]) noexcept
{
    float num = 0.0f, da = 0.0f, db = 0.0f;
    for (size_t i = 0; i < 3; ++i)
    {
        num += w[i] * a[i] * b[i];
        da  += w[i] * a[i] * a[i];
        db  += w[i] * b[i] * b[i];
    }
    if (da <= 1e-12f || db <= 1e-12f)
        return 0.0f;

    const float sim = num / std::sqrt (da * db);
    return juce::jlimit (-1.0f, 1.0f, sim);
}

void DnaLibrary::rankAgainst (const DnaFingerprint& query, float wLow, float wMid, float wHigh,
                              const juce::StringArray& boostRoles, float boostFactor)
{
    // Guard (Daedalus): all knobs at 0% makes weightedCosine return 0 for
    // every entry, so every score would read exactly 50% — misleading.
    // Fall back to equal weights so the list stays meaningful.
    float w[3] = { wLow, wMid, wHigh };
    if (w[0] + w[1] + w[2] < 1e-6f)
        w[0] = w[1] = w[2] = 1.0f;

    // Energy boost (v2.4): multiplicative, bounded (≤ 2× per spec), never a
    // filter — boosted entries rise, nothing is hidden. With an empty
    // boostRoles list the scores below are bit-identical to the unboosted
    // (v2.3) path.
    const float bf = juce::jlimit (1.0f, 2.0f, boostFactor);

    juce::SpinLock::ScopedLockType sl (lock);

    for (auto& e : entries)
    {
        const float sim = weightedCosine (query.macro, e.dna.macro, w);
        float score = (sim * 0.5f + 0.5f) * 100.0f;
        e.boosted = boostRoles.contains (e.role);
        if (e.boosted)
            score = juce::jmin (100.0f, score * bf);
        e.scorePct = score;
    }

    std::stable_sort (rankOrder.begin(), rankOrder.end(),
                      [this] (size_t a, size_t b)
                      {
                          if (entries[a].scorePct != entries[b].scorePct)
                              return entries[a].scorePct > entries[b].scorePct;
                          return entries[a].name < entries[b].name;
                      });
}

std::vector<DnaLibrary::RankedEntry> DnaLibrary::getSnapshot()
{
    juce::SpinLock::ScopedLockType sl (lock);

    std::vector<RankedEntry> out;
    out.reserve (rankOrder.size());
    for (size_t idx : rankOrder)
    {
        RankedEntry r;
        r.name     = entries[idx].name;
        r.scorePct = entries[idx].scorePct;
        r.boosted  = entries[idx].boosted;
        out.push_back (r);
    }
    return out;
}

bool DnaLibrary::getTopEntry (DnaFingerprint& dna, juce::String& name)
{
    juce::SpinLock::ScopedLockType sl (lock);

    if (rankOrder.empty() || entries.empty())
        return false;

    const auto& e = entries[rankOrder[0]];
    dna  = e.dna;
    name = e.name;
    return true;
}

std::vector<DnaLibrary::DebugEntry> DnaLibrary::getDebugSnapshot()
{
    juce::SpinLock::ScopedLockType sl (lock);

    std::vector<DebugEntry> out;
    out.reserve (rankOrder.size());
    for (size_t idx : rankOrder)
    {
        DebugEntry d;
        d.name     = entries[idx].name;
        d.dna      = entries[idx].dna;
        d.scorePct = entries[idx].scorePct;
        d.role     = entries[idx].role;
        d.boosted  = entries[idx].boosted;
        out.push_back (d);
    }
    return out;
}
