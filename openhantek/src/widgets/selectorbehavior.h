// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class QComboBox;

/// Enable consistent hover-wheel selection while preserving native combo-box
/// click and keyboard handling.
void enableSelectorWheelBehavior( QComboBox *comboBox );
