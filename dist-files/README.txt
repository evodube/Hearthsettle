Hearthsettle - stops loose clutter flying when a cell loads
Copyright (C) 2026 evodube

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program (LICENSE.txt, next to this file). If not, see
<https://www.gnu.org/licenses/>.

Source code: https://github.com/evodube/Hearthsettle


WHAT IT DOES
------------
When Skyrim loads a cell, Havok pushes apart loose objects that were placed
slightly inside tables, shelves or each other, and sometimes launches them.
Hearthsettle holds loose clutter steady right after each load so it eases
into place instead of flying, then hands it back to normal physics.

REQUIREMENTS
------------
- SKSE64
- Address Library for SKSE Plugins

Skyrim SE 1.5.97 and AE 1.6.x (Steam and GOG). Not for Skyrim VR.
No ESP. Nothing is saved, so it is safe to add or remove mid-playthrough.

FILES
-----
SKSE/Plugins/Hearthsettle.dll                the plugin
SKSE/Plugins/Hearthsettle.ini                settings (commented; defaults are tuned)
SKSE/Plugins/Hearthsettle/README.txt         this file
SKSE/Plugins/Hearthsettle/LICENSE.txt        GNU GPL v3.0 full text

LOG
---
Documents/My Games/Skyrim Special Edition/SKSE/Hearthsettle.log
If something still flies, please include this log in your bug report.

CREDITS
-------
CommonLibSSE-NG contributors (GPL-3.0-or-later with modding exception)
SKSE team
meh321 - Address Library for SKSE Plugins
