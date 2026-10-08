#pragma once

#include <dragon/Configuration.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/loader/Loader.hpp>

/// The payoff of the loader: go straight from a file on disk to a populated
/// Graph, with no manual architecture or base-address bookkeeping.
namespace dragon::loader {

/// Disassemble every executable section of `image` into a fresh Graph built for
/// the architecture the image declares. Each section is disassembled at its own
/// virtual address, so cross-section calls and jumps resolve. The entry point
/// and every function-typed symbol are registered as function starts.
///
/// Returns an empty Graph (architecture Unknown) if the image's architecture is
/// one dragon does not model - callers should check image.architecture() first
/// when that matters.
[[nodiscard]] Graph disassembleImage(const LoadedImage &image,
                                     const Configuration &config = {});

} // namespace dragon::loader
