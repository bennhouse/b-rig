#pragma once
// Tutto ciò che i sorgenti dei plugin includono dall'esterno (librerie standard, moduli JUCE, font) va
// incluso qui, FUORI dal namespace del modulo: quando il plugin lo include di nuovo da dentro
// "namespace rig_xxx { ... }" le guardie lo saltano e i simboli restano quelli globali.
// Le intestazioni del design system (smp/...) invece NON vanno incluse qui: ogni modulo ne ha una copia
// privata nel proprio namespace, con il proprio colore di accento.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "BinaryData.h"
