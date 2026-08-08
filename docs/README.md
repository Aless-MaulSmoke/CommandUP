# CommandUP (cup) Pipeline v1.0.1 — Upscale + Frame Interpolator 🎬

> **Lightweight, autonomous video post-processing pipeline built exclusively for Windows.** Bring the "Lossless Scaling" workflow to your offline local video files on low-end and legacy hardware.

> [!CAUTION]
> **OPERATING SYSTEM COMPATIBILITY:** CommandUP is engineered **strictly for Windows 10 and Windows 11 (64-bit)**. The core pipeline relies entirely on Windows-native PowerShell scripts (`.ps1`), Batch command-line wrappers (`.bat`), and Windows console host bindings. **Linux (including Wine/Proton environments) and macOS are NOT supported.**

---

## The Concept & Workflow 💡

Running a demanding game while simultaneously recording your gameplay in high resolution is a heavy workload that chokes entry-level computers. **CommandUP** solves this by segmenting your workflow:

1. **Record First:** Capture your gameplay at a modest, stable resolution (e.g., 720p at 30 FPS). This allows your PC to maintain Ultra graphics settings or Ray Tracing while playing.
2. **Process Later:** Submit the final recorded video to CommandUP to scale it up to **1080p at 60 FPS** or even **4K** offline.

### Why CommandUP is Different
Unlike mainstream tools that rely on heavy Artificial Intelligence, modern neural networks, and massive VRAM allocation, **CommandUP uses direct mathematical instructions running straight on your GPU silicon**. This brings fresh life to older setups—such as an AMD FX-6300 CPU and a 4GB Radeon RX 550 GPU—without overloading your hardware.

---

## 🛠️ Hardware & Software Requirements

To ensure total system stability and prevent memory crashes, your computer must meet the following requirements:

* **Vulkan API Compatibility (MANDATORY):** Your graphics card must have native support for Vulkan. Works on dedicated GPUs (AMD RX, Nvidia GTX/RTX) and recent integrated chips (AMD Vega/Intel HD Graphics). *If Vulkan is missing, the process will not start.*
* **Processor (CPU):** Any basic 4 or 6-core legacy processor (e.g., AMD FX or older Intel Core). Average CPU usage remains around 45%, keeping Windows smooth and free from overheating.
* **Video Memory (VRAM):** Legacy cards with **2GB of VRAM** are sufficient if used smartly. *Tip: Highly recommended to close web browsers, Discord, and background games before processing high resolutions (2K/4K) to prevent memory overflow.*
* **Operating System:** Windows 10 or Windows 11 (64-bit) with an active PowerShell terminal allowed to execute scripts.

---

## 📦 Installation & Setup

1. Download the project `.zip` package from the repository releases.
2. Extract the archive into any directory to reveal the `CommandUP` folder structure.
3. Open a standard **PowerShell** window (Administrator privileges are **not** required).
4. If this is your very first time executing automated scripts on your computer, type the following command to allow execution:

```powershell
Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
```
⚠️ **Action Required:** Type **YES** (`Y`) when prompted by the terminal to apply the temporary execution policy.

---

## 🚀 How to Run the Pipeline

### Method 1: Drag & Drop (User-Friendly Interface)
*Tailored for users who have no experience with terminal commands.*

1. Navigate to your extracted `CommandUP` folder and locate the file named `DRAG_HERE.bat`.
2. Open another file explorer window and drag your input over `DRAG_HERE.bat`:
   * **Single File:** Drag and drop your video file to process it alone.
   * **Full Folder:** Drag and drop a folder containing multiple videos to process all files in batch mode automatically.

#### Customizing Drag & Drop Settings
You can modify the default processing behavior by editing the `DRAG_CONFIG.txt` file:
```ini
[DRAG_SETTINGS]
QUALITY=med
FPS=60
INTERPOLATE=oversample
SCALE=1920x1080
SHARPNESS=8
CODEC=avc
HDR=false
SHUTDOWN=false
```
### Method 2: PowerShell Terminal (Advanced Control)
Navigate to your project directory inside PowerShell:
```powershell
cd "D:\CommandUP"
```

#### 1. `cup.ps1` (Main Engine Script)
This is the core modular script used for upscaling and frame interpolation. Customize it using the following command-line arguments:

* `-file "path\video.mp4"`: Absolute path to the source video file (supports `.mp4` and `.mkv`).
* `-folder "path\directory"`: Absolute path to a folder for batch video processing.
* `-scale factor|resolution`: Target dimension. Can be a decimal multiplier (e.g., `1.5` scales 720p to 1080p) or a literal resolution string (e.g., `"1280x720"`, `"1920x1080"`, `"2560x1440"`, `"3840x2160"`). If omitted, upscaling is skipped.
* `-fps [number]`: Target framerate (e.g., `60`). If omitted, frame generation is skipped.
* `-interpolate "none|oversample|mitchell_clamp|linear"`: Controls temporal smoothness. Defaults to `"none"`.
  * `none`: Pure frame duplication (Nearest Neighbor). Maximum GPU savings, crisp visuals, absolutely no ghosting.
  * `oversample`: Smooth Motion sampling. Blends frames only when needed, preserving the natural appearance.
  * `mitchell_clamp`: High-quality smooth interpolation. Eliminates ringing and artifacts (requires modern GPU/RTX).
  * `linear`: Linear blend overlay. Extremely lightweight, ideal for older configurations.
* `-quality "low|med|big"`: Compression profile. Defaults to balanced `med`.
  * `low`: Low VRAM overhead, aggressive space-saving, small file size.
  * `med`: Sweet spot. Preserves edge sharpness without bloating storage.
  * `big`: Maximum visual fidelity and high bitrate, meant for archival.
* `-sharpness [0-10]`: FSR sharpness filtering strength, ranging from `0` to `10`.
* `-codec "avc|hevc"`: Selects the video encoding standard for the output file.
  * `avc`: Advanced Video Coding (H.264). Recommended for maximum compatibility and older/legacy graphics cards.
  * `hevc`: High Efficiency Video Coding (H.265). Provides better compression and smaller file sizes. Required for HDR mode.
* `-hdr "false|true"`: Enables High Dynamic Range processing. *Warning: This is NOT "High Definition" (HD). Enabling this on standard non-HDR videos will distort colors and brightness. Requires hevc codec.*
  * `false`: Standard color space.
  * `true`: Forces FSR to use PQ color space (only use if source video is HDR10/PQ).
* `-shutdown "false|true"`: Computer shutdown behavior. Defaults to `false`.
  * `true`: Triggers a 30-second countdown upon completion. Press `[ENTER]` to shut down immediately, or `[ESC]` to cancel.
* `-hud_port [number]`: Overrides the default TCP port used for HUD communication.
* `-verbose`: Overrides the default logging behavior to enable detailed verbose output.
* `-gpu_id`: If you have more than one video card, specify which one you want to use via the video card ID number. Usually 0 for the first vCard, 1 for the second.

[!IMPORTANT]
🔒 **Safety Rule:** You must specify either `-file` or `-folder` (never both). The script strictly requires at least one action parameter: `-scale` or `-fps`.

##### Terminal Command Examples:
* **Upscale Only (720p to 1080p, medium sharpness):**
  ```powershell
  .\cup.ps1 -file "C:\path\video.mp4" -scale 1.5 -sharpness 5
  ```
* **Frame Generation Only (30fps to 60fps with linear blending):**
  ```powershell
  .\cup.ps1 -file "C:\path\video.mp4" -fps 60 -interpolate linear
  ```
* **Full Pipeline (1080p Upscale + 60 FPS + Mitchell Clamp):**
  ```powershell
  .\cup.ps1 -file "C:\path\video.mp4" -scale "1920x1080" -fps 60 -interpolate mitchell_clamp
  ```
* **Batch Processing Folder (Big Quality + 1.5x Upscale + Max Sharpness):**
  ```powershell
  .\cup.ps1 -folder "C:\my_videos\" -quality big -scale 1.5 -sharpness 10
  ```

##### Output File Naming Convention
Output videos are saved directly in the source directory using dynamic suffixes:
* **Upscale only:** `video_FSR_1080x720.mp4`
* **Frames only:** `video_IFS_60fpsLINEAR.mp4`
* **Full pipeline:** `video_FSR_1080x720_IFS_60fpsLINEAR.mp4`

---

## 📊 HUD, Logs, and Global Configuration

### Real-Time HUD Monitoring
The pipeline handles videos using FFmpeg in the background. However, processing data is streamed step-by-step to the **CommandUP HUD** interface through a local **TCP socket connection**. 

### Global Settings (`config.ini`)
You can tweak global parameters by editing the `config.ini` file located at the project root:

```ini
[CUP_SETTINGS]
HUD_PORT=4867
VERBOSE=false
GPU_ID=0
```

---

## 🔍 Quality Inspection Tool (`extract.ps1`)

An utility script designed to extract specific video segments frame-by-frame into image files, allowing precise visual quality assessments.

```powershell
.\extract.ps1 -file "C:\path\video.mp4" [arguments]
```
* `-file "path"`: Absolute path to the processed video.
* `-time "hh:mm:ss"`: Timestamp to start the extraction (Default: `00:00:03`).
* `-secs [number]`: Duration of the clip to convert into images (Default: `1` second, which yields 60 image files if the video runs at 60 FPS).
* `-output "path"`: Target directory for the images (Default: creates an `output` folder in the project root).

---

## ⚠ Known Limitations & Technical Artifacts

### Blurry or Softened Static Frames (Freeze Frame Artifacts)
* **Symptom:** Pausing the final video during fast-motion sequences can show a soft, blurry, or "ghosting" effect.
* **Cause:** The media player froze exactly on an intermediate, interpolated frame mathematically generated by the temporal shader logic.
* **Solution:** *This behavior is expected by design.* It is the necessary mechanic to guarantee smooth motion perception on low-end systems without requiring heavy machine learning hardware or Tensor Cores.

[!WARNING]
📝 **Important Compatibility Note:** FSR technology is fine-tuned for digital graphics. This pipeline is strictly optimized for **video game gameplays** (polygon edges, texture rendering). Applying it to real-life camera recordings, movies, or series will break the processing logic and yield poor results.

---

## 📝 Credits & Licensing

* **Core Component (FFmpeg):** Build N-125258-gdf94900c98-20260624. Licensed under *GNU LGPL v2.1+* or *GNU GPL v2.0+*.
* **Rendering Engine and Interpolate (libplacebo):** Compiled natively inside the core binary. Licensed under *GNU LGPL v2.1+*.
* **Spatial Scaling (AMD FidelityFX FSR v1.0.2):** Developed by AMD Inc. Distributed under the *MIT License* (port GLSL by agyild).
* **Pipeline Integration & Automation Scripts:** Developed by **Aless (MaulSmoke)**. Distributed under the *MIT License*.

### Support & Community
* **Pipeline Integration, Concept:** Aless (MaulSmoke).
* **Official YouTube Channel:** Watch tutorials, benchmarks, performance showcases, and interact with the community: [YouTube (@toplayaless)](https://www.youtube.com/playlist?list=PLae7RZ7VAOWk).
* **Official Reddit Community:** Share your experience, ask questions, and engage with other users: [Reddit (r/CommandUP)](https://www.reddit.com/r/CommandUP/).
* **Official Buy me a Coffee:** CommandUP is free, but development is limited. Consider buying me a coffee: [buymeacoffee (CommandUP)](https://buymeacoffee.com/commandup).
```
