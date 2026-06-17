# Main_MiSTer Main Binary and Wiki Repo

This repo serves as the home for the MiSTer Main binaries and the Wiki.

For the purposes of getting google to crawl the wiki, here's a link to the (not for humans) [crawlable wiki](https://github-wiki-see.page/m/MiSTer-devel/Wiki_MiSTer/wiki)

If you're a human looking for the wiki, that's [here](https://github.com/MiSTer-devel/Wiki_MiSTer/wiki)

To compile this application, read more about that [here](https://mister-devel.github.io/MkDocs_MiSTer/developer/mistercompile/#general-prerequisites-for-arm-cross-compiling)

---

## 2026-06-18 Updates

### 1. WSL2/Linux x86 Compatibility & Virtual Handshake Mocking
- **WSL Compilation & Execution (`make WSL=1`)**: Fixed core hardware memory accessing errors (Unable to open `/dev/mem` falling back to virtual mock memory) and allowed interactive execution inside WSL SDL2 window environment.
- **Handshake and Rendering Mocks**: Mocked hardware SPI handshake loops and correctly initialized OSD flags (`osd_is_visible = 1`) on boot under WSL.
- **Dynamic 3x4 Pixel Scaling**: Configured a dynamic 3x4 scaling stretch for the OSD pixel buffer overlay rendering to display menu text correctly inside the host graphic interface.
- **Key Event Forwarding**: Redirected keyboard events correctly to the OSD menu queue when keyboard inputs were intercepted by the SDL2 window wrapper under WSL.

### 2. Dynamic 8x8 Hangul Vowels/Consonants Combination Font Engine
- **Galmuri7 Porting**: Integrated the retro 8x8 Korean dot font (Galmuri7) database, including 19 onset consonants (6 variants), 21 nucleus vowels (2 variants), and 27 coda final consonants.
- **Dynamic 8-Bit Index Mapping**: Syllables are parsed from UTF-8 strings and dynamically mapped to unused extended ASCII slots `151` ~ `255` in the `charfont` table, maintaining string byte alignment and `strlen` layouts.
- **Caching Mechanism**: Dynamically updates the font ROM on-the-fly and automatically resets the slots when the menu is cleared (`OsdClear`) to prevent slot leaks.
- **OSD Translations**: Implemented an automated dictionary for prefix translations (e.g., `No files!` -> `파일 없음!`, `System Settings` -> `시스템 설정`).
- **WSL Terminal Printing**: Integrated dynamic UTF-8 reverse mapping to correctly print Korean characters within the mock terminal logging interface.
- **Signed Character Indexing Bugfix**: Fixed memory out-of-bounds crashes caused by sign-extension when querying `charfont` with character codes greater than 127.
