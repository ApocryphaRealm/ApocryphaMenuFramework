# Third-party components and their notices

Apocrypha Menu Framework as a whole is GPL-3.0-or-later (`LICENSE`, `NOTICE.md`). These components are included under their own
GPL-compatible licences; their notices are reproduced as those licences require.

## CommonLibSSE-NG 7.2.0 - Skyrim 1.7.x build line

https://github.com/alandtse/CommonLibSSE-NG (commit 7a60f4de794095d7b0f8928d1b930a52e9a7da83), GPL-3.0-or-later WITH
Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source); the exceptions ship as
`CommonLibSSE-NG-EXCEPTIONS.md` beside the 1.7 build.

## CommonLibSSE-NG 3.7.0 - SE 1.5.97 / AE 1.6.1170 build line

MIT License

Copyright (c) 2018 Ryan-rsm-McKenzie

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Font Awesome Free 6.7.2 - icon fonts (`SKSE/Plugins/ApocryphaMenuFramework/icons/`)

https://fontawesome.com - `fa-solid-900.ttf`, `fa-regular-400.ttf` and `fa-brands-400.ttf` from the official npm package
@fortawesome/fontawesome-free 6.7.2, unmodified (source and hashes: `extern/fontawesome-free-6.7.2/SOURCE.txt`). They are
separate font files read at run time, not part of the program, and ship under their own licence:

Copyright (c) 2024 Fonticons, Inc. (https://fontawesome.com) with Reserved Font Name: "Font Awesome". The font files are
licensed under the SIL Open Font License, Version 1.1 (the icons in the package's SVG/JS forms are CC BY 4.0, its code
MIT). The full licence text ships beside the fonts as `LICENSE-FontAwesome-Free.txt`. Brand icons are trademarks of their
respective owners.

## nanosvg - SVG textures for SKSE Menu Framework mods (`include/nanosvg/`, 2.1.3)

https://github.com/memononen/nanosvg - `nanosvg.h` and `nanosvgrast.h`, unmodified (the copies Wheeler-Refined carries).
They let `LoadTexture` read the `.svg` icons some SKSE Menu Framework mods ship (Walk With Me). zlib licence, from the
files' own headers:

Copyright (c) 2013-14 Mikko Mononen memon@inside.org. This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages arising from the use of this software. Permission
is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions: 1. The origin of this software must not be misrepresented;
you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the
product documentation would be appreciated but is not required. 2. Altered source versions must be plainly marked as
such, and must not be misrepresented as being the original software. 3. This notice may not be removed or altered from
any source distribution. The SVG parser is based on the Anti-Grain Geometry 2.4 SVG example, Copyright (C) 2002-2004
Maxim Shemanarev (McSeem); the rasteriser on stb_truetype's by Sean Barrett.

## DevBench consumer API (`include/DevBench/`, `source/DevBench/`)

MIT - the notice is `include/DevBench/DevBenchAPI.LICENSE.txt`, kept with the files.

## Notes carried from the previous licence file

Third-party components, each under its own permissive licence:

* Dear ImGui (MIT) - https://github.com/ocornut/imgui - 1.90.8 (docking), built with one change of ours since 2.1.6: an art hook (cmake/ports/imgui/amf-art-hooks.patch) that lets the framework draw its art parts in place of ImGui's boxes, buttons and other shapes
* CommonLibSSE-NG (MIT) - https://github.com/CharmedBaryon/CommonLibSSE-NG
* DevBenchAPI header/source (MIT) - the consumer API of DevBench, vendored so the framework can
  register its DevBench driving tools; devbench.dll itself is a separate, optional, GPL program
  that this framework only talks to over its REST API.

Compatibility note: Apocrypha Menu Framework exports an API compatible with the PUBLIC consumer
header of SKSE Menu Framework so that mods written against that header can register with it. It
is an original implementation and contains no code from SKSE Menu Framework.
