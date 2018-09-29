/*
 * DISTRHO Kars Plugin, based on karplong by Chris Cannam.
 * Copyright (C) 2015 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "DistrhoPluginKars.hpp"

START_NAMESPACE_DISTRHO

// -----------------------------------------------------------------------

DistrhoPluginKars::DistrhoPluginKars()
    : Plugin(paramCount, 0, 0), // 0 programs, 0 states
      fSustain(false),
      fSampleRate(getSampleRate()),
      fBlockStart(0)
{
    for (int i=kMaxNotes; --i >= 0;)
    {
        fNotes[i].voice = i;
        fNotes[i].setSampleRate(fSampleRate);
    }
}

// -----------------------------------------------------------------------
// Init

void DistrhoPluginKars::initParameter(uint32_t index, Parameter& parameter)
{
    if (index != 0)
        return;

    parameter.hints      = kParameterIsAutomable|kParameterIsBoolean;
    parameter.name       = "Sustain";
    parameter.symbol     = "sustain";
    parameter.ranges.def = 0.0f;
    parameter.ranges.min = 0.0f;
    parameter.ranges.max = 1.0f;
}

// -----------------------------------------------------------------------
// Internal data

float DistrhoPluginKars::getParameterValue(uint32_t index) const
{
    if (index != 0)
        return 0.0f;

    return fSustain ? 1.0f : 0.0f;
}

void DistrhoPluginKars::setParameterValue(uint32_t index, float value)
{
    if (index != 0)
        return;

    fSustain = value > 0.5f;
}

// -----------------------------------------------------------------------
// Process

void DistrhoPluginKars::activate()
{
    fBlockStart = 0;

    for (int i=kMaxNotes; --i >= 0;)
    {
        fNotes[i].on  = kNoteNull;
        fNotes[i].off = kNoteNull;
        fNotes[i].velocity = 0;
    }
}

struct AudioMidiSyncHelper {
    float** outputs;
    uint32_t frames;
    const MidiEvent* midiEvents;
    uint32_t midiEventCount;
    uint32_t remainingFrames;
    uint32_t remainingMidiEventCount;

    AudioMidiSyncHelper(float** const o, uint32_t f, const MidiEvent* m, uint32_t mc)
        : outputs(o),
          frames(0),
          midiEvents(m),
          midiEventCount(0),
          remainingFrames(f),
          remainingMidiEventCount(mc) {}

    bool nextEvent()
    {
        if (remainingMidiEventCount == 0)
        {
            if (remainingFrames == 0)
                return false;

            for (uint32_t i=0; i<1; ++i)
                outputs[i] += frames;

            if (midiEventCount != 0)
                midiEvents += midiEventCount;

            frames = remainingFrames;
            midiEvents = nullptr;
            midiEventCount = 0;
            remainingFrames = 0;
            return true;
        }

        const uint32_t eventFrame = midiEvents[0].frame;
        DISTRHO_SAFE_ASSERT_RETURN(eventFrame < remainingFrames, false);

        midiEventCount = 1;

        for (uint32_t i=1; i<remainingMidiEventCount; ++i)
        {
            if (midiEvents[i].frame != eventFrame)
            {
                midiEventCount = i;
                break;
            }
        }

        frames = remainingFrames - eventFrame;
        remainingFrames -= frames;
        remainingMidiEventCount -= midiEventCount;
        return true;
    }
};

void DistrhoPluginKars::run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount)
{
    uint8_t note, velo;

    for (AudioMidiSyncHelper amsh(outputs, frames, midiEvents, midiEventCount); amsh.nextEvent();)
    {
        float* const out = amsh.outputs[0];

        for (uint32_t i=0; i<amsh.midiEventCount; ++i)
        {
            if (amsh.midiEvents[i].size > MidiEvent::kDataSize)
                continue;

            const uint8_t*  data = amsh.midiEvents[i].data;
            const uint8_t status = data[0] & 0xF0;

            switch (status)
            {
            case 0x90:
                note = data[1];
                velo = data[2];
                DISTRHO_SAFE_ASSERT_BREAK(note < 128); // kMaxNotes
                if (velo > 0)
                {
                    fNotes[note].on  = fBlockStart + amsh.midiEvents[i].frame;
                    fNotes[note].off = kNoteNull;
                    fNotes[note].velocity = velo;
                    break;
                }
                // fall through
            case 0x80:
                note = data[1];
                DISTRHO_SAFE_ASSERT_BREAK(note < 128); // kMaxNotes
                fNotes[note].off = fBlockStart + amsh.midiEvents[i].frame;
                break;
            }
        }

        std::memset(out, 0, sizeof(float)*amsh.frames);

        for (int i=kMaxNotes; --i >= 0;)
        {
            if (fNotes[i].on != kNoteNull)
                addSamples(out, i, amsh.frames);
        }
    }

    fBlockStart += frames;
}

void DistrhoPluginKars::addSamples(float* out, int voice, uint32_t frames)
{
    const uint32_t start = fBlockStart;

    Note& note(fNotes[voice]);

    if (start < note.on)
        return;

    if (start == note.on)
    {
        for (int i=note.sizei; --i >= 0;)
            note.wavetable[i] = (float(rand()) / float(RAND_MAX)) * 2.0f - 1.0f;
    }

    const float vgain = float(note.velocity) / 127.0f;

    bool decay;
    float gain, sample;
    uint32_t index, size;

    for (uint32_t i=0, s=start-note.on; i<frames; ++i, ++s)
    {
        gain = vgain;

        if ((! fSustain) && note.off != kNoteNull && note.off < i+start)
        {
            // reuse index and size to save some performance.
            // actual values are release and dist
            index = 1 + uint32_t(0.01 * fSampleRate); // release, not index
            size  = i + start - note.off;             // dist, not size

            if (size > index)
            {
                note.on = kNoteNull;
                break;
            }

            gain = gain * float(index - size) / float(index);
        }

        size  = uint32_t(note.sizei);
        decay = s > size;
        index = s % size;

        sample = note.wavetable[index];

        if (decay)
        {
            if (index == 0)
                sample += note.wavetable[size-1];
            else
                sample += note.wavetable[index-1];

            note.wavetable[index] = sample/2;
        }

        out[i] += gain * sample;
    }
}

// -----------------------------------------------------------------------

Plugin* createPlugin()
{
    return new DistrhoPluginKars();
}

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO
