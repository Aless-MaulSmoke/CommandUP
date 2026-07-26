# 📖 FSRIFS v1.1.2 Documentation: Concept & Practical Guide

Welcome to the official documentation for **FSRIFS** (FidelityFX Super Resolution + Interpolate Frame Sampling), a lightweight video post-processing pipeline designed specifically for low-end and legacy hardware.

---

## 💡 The Project Concept

### The Core Problem
Running a demanding game while simultaneously recording your gameplay in high resolution is a heavy workload that requires an extremely robust PC. Even high-end gaming rigs can struggle under this dual real-time processing stress. 

To solve this, the ideal workflow is to segment the process:
1. **Record first:** Capture your gameplay at a modest resolution (e.g., 720p at 30 FPS). This allows your PC to maintain ultra graphical settings, high framerates, or features like Ray Tracing while playing.
2. **Process later:** Submit the final video file to an offline upscaling and frame generation pipeline to achieve the desired high resolution and smooth motion.

### Why FSRIFS is Different
Most modern upscale and frame generation software depend heavily on complex Artificial Intelligence algorithms. These tools require powerful, cutting-edge CPUs and GPUs featuring dedicated AI accelerator hardware, alongside massive amounts of VRAM. For entry-level or legacy graphics cards, running these solutions is physically impossible.

**FSRIFS** changes the game. Inspired by the efficiency of *Lossless Scaling* in real-time gaming environments, this project adapts that exact principle for local video file processing. Instead of utilizing heavy AI neural networks, it replaces them with direct, lightweight mathematical instructions that run straight on your GPU silicon. This brings fresh life to older setups—such as an AMD FX-6300 CPU and a 4GB Radeon RX 550 GPU—allowing you to easily transform a 720p/30fps capture into a fluid 1080p/60fps or 4K master file without overloading your hardware.

---

## 🛠️ Hardware Requirements & Dependencies

To ensure total system stability and prevent crashes, your computer must meet the following minimum requirements:

* **Vulkan API Compatibility (MANDATORY):** Your graphics card must have native support for Vulkan technology. The custom IFS shader executes temporal calculations directly on the GPU through this API. It works on dedicated GPUs (AMD RX, Nvidia GTX/RTX) as well as recent integrated chips (AMD Vega or newer Intel HD Graphics). If Vulkan is missing, the pipeline will not start.
* **Processor (CPU):** Any basic 4 or 6-core legacy processor (such as the AMD FX line or older Intel Core generations) is fully capable. Average CPU usage remains around 45%, keeping Windows smooth and free from overheating during rendering.
* **Video Memory (VRAM):** Legacy cards with 2GB of VRAM are sufficient. However, it is highly recommended to close web browsers, Discord, and games in the background before processing high resolutions (2K or 4K) to prevent video memory overflow.
* **Operating System:** Windows 10 or Windows 11 (64-bit) with an active PowerShell terminal allowed to execute local scripts.

---

## 📦 Installation & Setup

1. Download the project `.zip` package from the repository releases.
2. Extract the archive into any directory of your preference to reveal the `FSRIFS` folder structure.
3. Open a standard **PowerShell** window (you do *not* need to run it as an Administrator; the scripts do not require elevated system privileges).
4. If this is your very first time executing automated scripts on your computer, type the following command into PowerShell to allow script execution:

```powershell
Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
```

> ⚠️ **Action Required:** Type **YES** (`Y`) when prompted by the terminal to apply the new temporary execution policy.
---

## 🚀 How to Run the Pipeline

Once your PowerShell environment is ready, you can process your videos using two different approaches:

### Method 1: Drag & Drop (User-Friendly Interface)
Tailored for users who have no experience with terminal commands.

1. Open your file explorer and navigate to your extracted `FSRIFS` folder.
2. Locate the file named `DRAG_HERE.bat`.
3. Open another file explorer window and choose your input:
   * **Single File:** Drag and drop your video file directly on top of the `DRAG_HERE` file to process it alone.
   * **Full Folder:** Drag and drop a folder containing multiple videos on top of the `DRAG_HERE` file to process all files in batch mode automatically.

#### Customizing Drag & Drop Settings
You can modify the default processing behavior by opening and editing the `DRAG_CONFIG.txt` configuration file:

```ini
[DRAG_SETTINGS]
SCALE=1920x1080
SHARPNESS=5
FPS=60
INTERPOLATE=oversample
QUALITY=med
VERBOSE=false
SHUTDOWN=false
```

---

### Method 2: PowerShell Terminal (Advanced Control)
The original method for full control over processing parameters. Navigate to your project directory inside PowerShell (Example):

```powershell
cd "D:\FSRIFS"
```

Now you can interact with the two core scripts:

#### 1. `process.ps1` (Main Engine)
This is the main modular script used for upscaling and frame interpolation. You can customize the process using command-line arguments:

* `-file "path\video.mp4"`: The absolute path to your source video (must be enclosed in quotation marks).
* `-scale factor|resolution`: Target dimension. Can be a decimal multiplier (e.g., `1.5` scales 720p to 1080p) or a literal resolution (e.g., `"1920x1080"`). If omitted, upscaling is skipped.
* `-fps [number]`: Target framerate (e.g., `60`). If omitted, frame generation is skipped.
* `-interpolate "none|oversample|mitchell_clamp|linear"`: Defines the interpolation method. If omitted, defaults to "none".
* `-quality "low|med|big"`: Compression profile. Defaults to balanced `med`.
  * `low`: Focuses on aggressive space saving and reduces VRAM overhead. Small file size.
  * `med`: The ideal sweet spot. Preserves edge sharpness and fidelity without bloating storage.
  * `big`: Maximum visual fidelity and high bitrate, meant for archival. Generates massive files.
* `-sharpness [0-10]`: FSR sharpness filtering layer. Ranges from `0` (min) to `10` (max).
* `-v`: Verbose mode. Prints detailed processing logs directly to the console.
* `-shutdown`: Shuts down the computer after the process finishes..

> 🔒 **Safety Rule:** You must specify at least one action parameter: either `-scale` or `-fps` for the script to execute.

##### Command Examples:
* **Upscale Only (720p to 1080p, medium sharpness):**
```powershell
.\process.ps1 -file "C:\path\video.mp4" -scale 1.5 -sharpness 5
```
* **Frame Generation Only (30fps to 60fps):**
```powershell
.\process.ps1 -file "C:\path\video.mp4" -fps 60 -interpolate=linear
```
* **Full Processing Pipeline (1080p Upscale + 60 FPS + Detailed Logs):**
```powershell
.\process.ps1 -file "C:\path\video.mp4" -scale "1920x1080" -fps 60 -interpolate="mitchell_clamp" -v 
```

##### Output File Naming Convention
The output video is automatically saved in the same directory as your source file using intelligent suffixes:
* **Upscale only:** `video_FSR_1080x720.mp4`
* **Frames only:** `video_60fps.mp4`
* **Full pipeline:** `video_FSR_1080x720_60fps.mp4`

---

#### 2. `extract.ps1` (Quality Inspection Tool)
An optional utility script designed to extract video frames into separate image files, allowing you to examine the visual quality frame-by-frame.

```powershell
.\extract.ps1 -file "C:\path\video.mp4" [arguments]
```

* `-file "path"`: The full path to the video (enclosed in quotation marks).
* `-time "hh:mm:ss"`: Start time for extraction. Defaults to `00:00:03`.
* `-secs [number]`: Extraction duration in seconds. Defaults to `1` second (extracts 60 frames if the video is 60 FPS).
* `-output "path"`: Destination directory. Defaults to the `output` folder in the project root.

---

## ⚠️ Known Limitations & Technical Artifacts

### Blurry or Softened Static Frames (Freeze Frame Artifacts)
* **Symptom:** When pausing the final video during fast-motion sequences, the static frame may look soft, blurry, or show a "ghosting" effect.
* **Causa:** Your media player paused precisely on an intermediate, interpolated frame mathematically generated by the temporal shader logic.
* **Solution:** This behavior is expected by design. It is a necessary mechanic to guarantee the illusion of motion smoothness on low-end systems without requiring heavy hardware neural engines.

> 📝 **Important Note:** FSR technology is fine-tuned for digital graphics. This pipeline is strictly optimized for **video game gameplays** (polygon edges, texture rendering). Applying it to real-life camera recordings or movies will break the processing logic and yield poor results.

## 📌 Updates & Version History
See the [CHANGELOG.md](docs/CHANGELOG.md) file for full details on recent bug fixes and performance improvements.

---

## 📝 Credits & Licensing

* **Core Component (FFmpeg):** Build N-125258-gdf94900c98-20260624. Licensed under *GNU LGPL v2.1+* or *GNU GPL v2.0+*.
* **Rendering Engine and Interpolate (libplacebo):** Compiled natively inside the core binary. Licensed under *GNU LGPL v2.1+*.
* **Spatial Scaling (AMD FidelityFX FSR v1.0.2):** Developed by AMD Inc. Distributed under the *MIT License* port glsl by agyild.

### Support & Community
* **Pipeline Integration, Concept:** Aless (MaulSmoke).
* **Official YouTube Channel:** Watch tutorials, benchmarks, performance showcases, and interact with the community: [YouTube (@toplayaless)](https://www.youtube.com/playlist?list=PLae7RZ7VAOWk).
* **Official Reddit Community:** Share your experience, ask questions, and engage with other users: [Reddit (r/FSRIFS)](https://www.reddit.com/r/FSRIFS/).
