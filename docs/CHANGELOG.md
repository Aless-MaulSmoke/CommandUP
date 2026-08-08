# 📜 CHANGELOG: CommandUP & FSRIFS History

All notable changes to this project will be documented in this file. This project adheres to Semantic Versioning.

---

## ☕ CommandUP Era

### CUP [v1.0.1] - 2026-08-06
* **Added `-codec` Parameter:** Added native support for selecting the video encoding standard. **avc** for (H.264) legacy hardware or maximum device compatibility, and **hevc** for (H.265) better compression efficiency, smaller file sizes, and high dynamic range processing.
* **[NVIDIA] Critical Fix for Error -22 via Vulkan/libplacebo:** Addresses a change in NVIDIA driver behavior where native allocation of multiple image planes is rejected in current drivers. A fix was implemented using a flag that resolves the GeForce driver's memory intolerance without requiring invasive pixel format conversions.


### CUP [v1.0.0] - 2026-08-03
* **Project Renamed:** *FSRIFS* has been officially renamed to **CommandUP**, with the abbreviation **(cup)**. This change brings a fresh perspective and simplifies the pronunciation of the pipeline. May CommandUP become an incredible tool for everyone.
* **New `-gpu_id` Parameter:** For multi-GPU configurations, you can now specify which graphics card to use via its hardware ID number. This parameter can be set dynamically in the console (`-gpu_id`) or globally by editing the `config.ini` file.
* **HUD Layout Update:** Adjusted and refined the visual format in which real-time video processing statistics are displayed.

---

## 🛡️ FSRIFS Legacy Era

### FSRIFS [v1.2.0] - 2026-07-29
* **Script Refactoring:** Renamed the core execution script from `process.ps1` to `fsrifs.ps1`.
* **Socket-Based HUD Architecture:** Offloaded the HUD rendering workload from FFmpeg. The core engine now streams raw processing statistics via a local TCP socket. FSRIFS reads this live data stream to render a much cleaner, responsive, and coherent terminal HUD.
* **New Configuration File:** Introduced `config.ini` to the project root, allowing users to manually bind and customize the specific TCP port used for HUD communication.
* **New `-hud_port` Parameter:** Added terminal level overrides for the communication port via console (`-hud_port`) or by editing the `config.ini` file.
* **Verbose Log Relocation:** Moved the `-v` (Verbose) flag settings from `Drag_Config.txt` into the global `config.ini` file. The parameter remains accessible via the terminal using `-verbose`. Its behavior was optimized: complex debug outputs no longer clutter the active terminal HUD; all diagnostic data is now routed strictly to the background log file.

### FSRIFS [v1.1.2] - 2026-07-26
* **Timer Bug Fix:** Resolved a calculation inconsistency that caused incorrect formatting when displaying the original video length alongside the total elapsed processing time.
* **Final Report Enhancement:** The execution wrap-up report now reliably displays comprehensive metrics for folder-wide batch processing, even if only a single valid video file was present.
* **Shutdown Handshake Error Fix:** Fixed an edge-case bug that completely halted the 30-second computer shutdown countdown if the terminal window lost active window focus.
* **Fake Warning Mitigation:** Suppressed phantom warnings that triggered when piping custom arguments directly through the console without explicitly defining a `-drag` config payload.

### FSRIFS [v1.1.1] - 2026-07-21
* **Bug Fix / Hardware Support:** Added native support for integrated AMD Vega graphics processing units.

### FSRIFS [v1.1.0] - 2026-07-20
* **Architecture Refactoring:** Fully migrated the internal codebase from a rigid procedural structure to a clean, modular architecture for better long-term maintainability and faster command execution.
* **Multi-Vendor GPU Support:** Eliminated restrictive hardware lock assumptions. The pipeline, which previously ran exclusively on AMD graphics cards, now fully supports NVIDIA and Intel hardware (both dedicated and integrated chips) through the Vulkan API. Output videos are now encoded in H.264 format.
* **Color Format Metadata:** Generated videos now actively inherit the color format of the original source video, while enforcing a strict Limited Color Range format for maximum metadata stability.
* **Native Core Interpolation Migration:** Retired the custom internal shader due to edge-case compatibility constraints. The pipeline now natively utilizes libplacebo's high-performance algorithms (`oversample`, `linear`, `mitchell_clamp`) directly via custom FFmpeg filterchains.
* **Acronym Re-definition:** Formally redefined the **IFS** acronym to stand for **Interpolated Frame Sampling** to better align with the new native backend architecture.
* **Automated Shutdown Feature:** Implemented an automatic system shutdown sequence that triggers immediately after video post-processing completes.
* **Added `-drag_config` Flag:** Enabled support for dynamically pulling runtime settings straight from the local `DRAG_CONFIG.txt` configuration asset.
* **Added `-folder` Batch Processing:** Introduced automated folder-wide batch processing, allowing the execution script to queue and process separate video targets sequentially.

### FSRIFS [v1.0.1] - 2026-07-14
* **Filterchain Race Condition Fix:** Fixed a critical bug where executing FSR and IFS concurrently caused the FSR upscale shader block to be skipped entirely, resulting in basic canvas stretching with no actual edge reconstruction.

### FSRIFS [v1.0.0] - 2026-07-10
* **Initial Release:** Official public launch of the standalone FSR + IFS video post-processing pipeline ecosystem.
