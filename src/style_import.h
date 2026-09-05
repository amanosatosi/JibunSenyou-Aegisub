// Copyright (c) 2026

#pragma once

class AssFile;
class AssStyle;

/// Import one style, optionally replacing a same-name style.
/// Returns true if the destination changed.
bool ImportStyle(AssFile& destination, AssStyle const& source, bool replace_existing);
