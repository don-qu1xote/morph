<h1 align="center">morph</h1>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++" />
  <img src="https://img.shields.io/badge/Qt-41CD52?style=for-the-badge&logo=qt&logoColor=white" alt="Qt" />
  <img src="https://img.shields.io/badge/QML-41CD52?style=for-the-badge&logo=qt&logoColor=white" alt="QML" />
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL_v3-blue.svg?style=for-the-badge" alt="License" /></a>
  <img src="https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-blue?style=for-the-badge" alt="Linux | macOS" />
  <a href="https://github.com/don-qu1xote/morph/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/don-qu1xote/morph/ci.yml?style=for-the-badge&label=CI" alt="CI" /></a>
  <a href="https://www.donationalerts.com/r/lizapropanol"><img src="https://img.shields.io/badge/Donate-DonationAlerts-orange?style=for-the-badge&logo=donate&logoColor=white" alt="Donate" /></a>
</p>

> This is a fork of [lizapropanol/morph](https://github.com/lizapropanol/morph). On top of upstream it adds a macOS build, honest sync with Yandex Music (real listening stats, My Vibe feedback, two-way likes), playback of tracks removed from Yandex via YouTube and SoundCloud, unit tests and CI. See [docs/architecture.md](docs/architecture.md) and [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

**morph** is a sleek, modern desktop music player built with C++ and Qt/QML. It integrates seamlessly with **Yandex Music**, **SoundCloud** and **YouTube Music**, providing a unified and aesthetically pleasing interface for your music library, charts, and personalized radio streams.

---

## Key Features

- **Triple Service Integration**: Search and play tracks from Yandex Music, SoundCloud and YouTube Music in a single interface.
- **My Vibe (Personalized Wave)**: Direct integration with Yandex Music's algorithmic radio, complete with listening telemetry so your recommendations keep improving.
- **Waves**: Start a wave from any track or artist, play Yandex stations (activities, genres, moods) from the home screen and tune My Vibe by character, mood and language.
- **Mixed Wave**: Similar tracks from SoundCloud and YouTube between the Yandex ones, plus liked tracks Yandex has removed. Yandex tracks keep their order and feedback; turn **Mixed Wave** off in Settings for the plain Yandex wave.
  - Runs through rotor sessions and reports start, skip (with real listened seconds), finish, like and dislike back to Yandex.
  - Likes are synced with your Yandex account both ways.
- **Removed Tracks Fallback**: If Yandex no longer serves a track, morph looks for the same recording on SoundCloud and YouTube, compares title, artist and duration so live versions, covers and remixes lose to the original, and plays it under the original track. A badge next to the bitrate shows where the track plays from; click it to switch to the next best version, and the choice is remembered, so playlists and likes keep working.
- **Fast YouTube Start**: The next track is found and downloaded while the current one plays, and yt-dlp asks YouTube with a single lightweight client, so tracks from YouTube start almost as fast as Yandex ones.
- **Text List Import**: Paste a path to a `.txt` file with `Artist - Title` lines into the import field to get a playlist played through search.
- **Top Charts**: Browse and play the top tracks from Yandex Music with a clean, dual-column ranked layout.
- **For You**: Yandex personal playlists (Playlist of the Day, Premiere, Deja Vu, Stash) and fresh editorial picks on the home screen, next to SoundCloud daily mixes.
- **Artist Profiles**: Browse detailed artist profiles displaying a biography, high-resolution avatar, list of albums, and tracks directly from the player.
- **Advanced Playlist Management**:
  - Create, rename, and delete custom playlists.
  - Set custom cover images using external URLs (Imgur, Pinterest, etc.).
  - Easily add tracks from search, charts, or history directly into your playlists.
- **Search History**: Automatically keeps track of your 20 most recently played tracks from searches for quick access.
- **Persistent Caching & Offline Support**: Automatically saves played tracks and viewed covers to local storage, enabling full playback without an internet connection.
- **Advanced Cache Management**: Dedicated storage view with real-time size calculation and granular clearing options for tracks and covers.
- **Real-time Bitrate Display**: Shows current track quality and bitrate information directly in the interface for all supported services.
- **Security & Privacy**:
  - **Hardware-Linked Encryption**: Authentication tokens are protected with dual-layer AES-256-GCM encryption. Keys are derived via PBKDF2-HMAC-SHA-512 (600,000 iterations) from a combination of system-unique identifiers, ensuring your credentials remain secure and unusable if copied to another machine.
- **System Integration**:
  - **Discord Rich Presence**: Show your current track, artist, and playback status directly on your Discord profile.
  - **MPRIS Support**: Full integration with Linux media controllers (playerctl, system tray, etc.) for remote playback control and metadata display.
- **Extensible Styling Engine**:
  - **Config Library**: A built-in library to manage, import, and export custom QML themes and configurations effortlessly.
  - **Live "Run Preview"**: Instantly launch a separate window to test and visualize your style changes in real-time as you edit.
  - **Integrated IDE**: Includes a custom QML syntax highlighter and live preview system for theme development.
- **Modern UI/UX**:
  - Dark, minimalist aesthetic utilizing smooth gradients and QML animations.
  - Circular reveal splash screen synchronized with logo expansion.
  - Time-aware greetings with active service status indicators.
  - Real-time visual indicators (green dots) for cached offline tracks.
  - Master-Detail navigation flow for intuitive library browsing.
  - Interactive elements with consistent hover states and hand-cursor feedback.
- **Reactive Settings**: Automatic persistence of authentication tokens and audio preferences upon input.
- **Lightweight & Fast**: Powered by C++ backend logic for minimal resource usage while delivering a premium Qt Quick interface.

---

## Installation & Setup

### Arch User Repository (AUR)
If you are on Arch Linux, you can easily install **morph** using an AUR helper like `yay`:
```bash
yay -S morph
```

### Prerequisites
- CMake (3.16 or higher)
- Qt 6 (Core, Quick, Qml, Network, Multimedia, DBus, Svg, 5Compat)
- C++23 compatible compiler (GCC 12+ / Clang 15+)

### Runtime Dependencies (Arch Linux)
For full functionality (audio codecs and icons), make sure to install:
```bash
sudo pacman -S qt6-base qt6-declarative qt6-multimedia qt6-svg qt6-5compat gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav hicolor-icon-theme yt-dlp ffmpeg openssl
```

### Building from Source

#### Linux
```bash
# Clone the repository
git clone https://github.com/lizapropanol/morph.git
cd morph

# Create a build directory
mkdir build && cd build

# Configure and build
cmake ..
make

# Run the application
./morph
```

#### macOS
```bash
# deno is required by yt-dlp for YouTube
brew install qt cmake openssl yt-dlp ffmpeg deno

git clone https://github.com/don-qu1xote/morph.git
cd morph
make run      # Debug build, opens build/morph.app
make dmg      # Release build packed into build-release/morph.dmg
```
Media keys, AirPods controls and the Now Playing widget work through `MPRemoteCommandCenter`.

#### Windows
```powershell
# Prerequisites: Qt 6.8+ (MSVC 2022 x64), OpenSSL 3.x, Inno Setup 6 (optional, for installer)
# Inno Setup can be installed via: winget install JRSoftware.InnoSetup

# Quick build with PowerShell script (compiles morph and creates setup installer):
.\scripts\build_installer.ps1

# Or manual CMake build:
cmake -B build -S . -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64;C:/Qt/Tools/OpenSSLv3/Win_x64"
cmake --build build --config Release
# Installer will be generated at build/morph-v<version>-setup.exe
```

---

## Configuration

To unlock full functionality, you need to provide your authentication tokens in the **Settings** tab. Changes are saved automatically.

### Yandex Music Token
1. Use a browser extension like **yandex-music-token** (available for Chrome/Firefox), or open `https://oauth.yandex.ru/authorize?response_type=token&client_id=23cabbbdc6cd418abb4b39c32c41195d` and take `access_token` from the address you get redirected to.
2. Log in to your Yandex account.
3. Copy the token and paste it into the "Yandex Music Token" field in morph's settings. The token is valid for about a year.

### SoundCloud
The "SoundCloud Client ID" field accepts any of these:

- **client_id** (enough for search, playback and the removed tracks fallback). Open [soundcloud.com](https://soundcloud.com), press **F12**, go to **Network**, filter by `api-v2` and copy the `client_id=` value from any request. It is the same for everyone and changes every few months.
- **Current access token** (`eyJ...`). In the same request open **Headers** and copy the `Authorization` value, with or without the `OAuth ` prefix. It lives for 24 hours: morph uses it for personalized features like **Daily Mixes** while it is valid and keeps working on the `client_id` taken from inside it afterwards.
- **Legacy `oauth_token` cookie** (`2-...`), if your account still has one.

---

## Project Structure

- `src/core/`: Application initialization and service management.
- `src/services/`: Specific API implementations (`YandexService`, `SoundCloudService`, `YouTubeService`) and base interfaces.
- `src/utils/`: Cache management, path resolution, and file system utilities.
- `src/network/`: HTTP wrappers for API communication.
- `src/settings/`: JSON-based local storage management for playlists, likes, tokens, and history.
- `src/audio/`: Low-level audio engine integration.
- `ui/`: The frontend layer, containing `style.qml` and associated assets.
- `tests/`: QtTest unit tests, services run against a local fake HTTP server.
- `macos/`: Bundle Info.plist and icon.
- `docker/`: Build and test environment used locally and in CI.
- `docs/`: Architecture, development guide and ADRs.

---

## License

This project is open-source. Please see the `LICENSE` file in the root directory for more details.

---

## Star History

<a href="https://www.star-history.com/?repos=lizapropanol%2Fmorph&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=lizapropanol/morph&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=lizapropanol/morph&type=date&theme=dark&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=lizapropanol/morph&type=date&theme=dark&legend=top-left" />
 </picture>
</a>

---

<p align="center">Developed with ❤️ by <a href="https://github.com/lizapropanol">lizapropanol</a></p>
