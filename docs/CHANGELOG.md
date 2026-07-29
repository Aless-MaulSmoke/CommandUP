## [v1.2.0] - 2026-07-29

* **Script**: `process.ps1` renamed to `fsrifs.ps1`.
* **Socket-based HUD**: FFmpeg no longer renders the HUD; instead, it sends process statistics via a socket. FSRIFS reads this data and renders a more coherent HUD.
* **New configuration file**: `config.ini` created; allows setting the TCP port used for HUD communication.
* **New parameter `-hud_port`**: This parameter can be set via the console (`-hud_port`) or by editing `config.ini`.
* **`-v` (Verbose) parameter**: Moved from `Drag_Config.txt` to `config.ini`; still accessible via the console using `-verbose`. Its behavior has changed: the HUD is no longer cluttered with complex debug output; all information is now written solely to the log.


## [v1.1.2] - 2026-07-26

* **Timer bug**: Fixed an inconsistency in the display of the original video time and the total elapsed time.
* **Final report**: The process's final report now displays results for batches of files processed in the folder, even if only a single valid file was processed.
* **Shutdown error**: Fixed a bug that stopped the computer shutdown countdown if the terminal window did not have focus.
* **Fake warnings**: Fake warnings when sending parameters via the console without specifying -drag config.

## [v1.1.1] - 2026-07-21
* **Bug Fix / GPU Support**: Support for integrated AMD Vega is included as well.

## [v1.1.0] - 2026-07-20
* **Architecture Refactoring**: Fully migrated the internal codebase from a rigid procedural structure to a clean, modular architecture for better maintainability and faster command execution.
* **Bug Fix / Multi-GPU Support**: Resolved hardware lock restrictions. The pipeline, which previously ran exclusively on AMD graphics cards, now fully supports NVIDIA and Intel hardware (both dedicated and integrated chips) through the Vulkan API. The videos are now encoded in H.264 format.
* **Color format Metadata**: The video generated now follows the same color format as the original video, but the Color Range must necessarily follow the Limited format.
* **Migration to Native Interpolation (Core Engine)**: Retired the custom internal shader due to compatibility constraints. The pipeline now natively utilizes libplacebo's high-performance algorithms (`oversample`, `linear`, `mitchell_clamp`) via FFmpeg filters.
* **Acronym Meaning Update**: Redefined the **IFS** acronym to mean **Interpolated Frame Sampling** to align with the new native backend architecture.
* **Added Shutdown parameter**: New feature for automatically shutting down the computer once the video post-processing is finished.
* **Added -drag_config parameter**: Allows dynamically loading all pipeline settings straight from the `DRAG_CONFIG.txt` configuration file.
* **Added -folder parameter**: New feature for batch processing files. (Send a folder with separate videos to process).

## [v1.0.1] - 2026-07-14
* Fixed a bug where using FSR + IFS simultaneously could cause the FSR upscale to be ignored, resulting in a basic image stretching without real processing.

## [v1.0.0] - 2026-07-10
