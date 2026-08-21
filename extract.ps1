# ==========================================================================
# ------------------------------
#
#   CommandUP (cup) Pipeline - Core Processing Engine (v1.0.2)
#   Automated pipeline for video upscaling (FSR) and frame interpolation (IFS).
#
#   extract.ps1 (Powershell script)
#   07/15/2026
#   by Aless (MaulSmoke)
#
#   A modular script designed to orchestrate lightweight, hardware-accelerated 
#   Vulkan workflows for video post-processing without heavy AI dependencies.
#
#  -file "string"
#   The absolute or relative path to the input video file.
#
#  -time "string"
#   the exact start time for frame extraction (hh:mm:ss).
#
#  -secs  number
#   Number of seconds you wish to extract frames.
#
#  -output "string"
#   Location of the folder to store extracted frames.
#
# ------------------------------

param(
    [string]$file,
    [string]$time = "00:00:03",   # Desired start time
    [int]$secs = 1,               # Extraction duration
    [string]$output               # Destination folder (optional)
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

# ==========================================================================
# SETUP PATHS & DIRECTORIES
# ==========================================================================
# Ensures dynamic absolute paths based on the script's current location
$ffmpegPath  = Join-Path $PSScriptRoot "ffmpeg\bin\ffmpeg.exe"
$ffprobePath = Join-Path $PSScriptRoot "ffmpeg\bin\ffprobe.exe"

if ([string]::IsNullOrEmpty($output)) {
    $outputDir = Join-Path $PSScriptRoot "output"
} else {
    $outputDir = $output
}

# Make sure the destination folder exists
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

# ==========================================================================
# SETUP VALIDATIONS (ANINHADO & BLINDADO)
# ==========================================================================

# 1. Validation of Input File and Dependencies
if (-not $file) {
    Write-Warning "No file information. Use the '-file' parameter to specify the video."
    exit
} else {
    if (-not (Test-Path -Path $file -PathType Leaf)) {
        Write-Warning "The specified file does not exist: '$file'"
        exit
    }
    
    $AllowedExtensions = @(".mp4", ".mkv", ".avi", ".mov", ".flv", ".webm", ".wmv", ".m4v")
    $FileExtension = [System.IO.Path]::GetExtension($file).ToLower()

    if ($FileExtension -notin $AllowedExtensions) {
        Write-Warning "The file '$FileExtension' is not a supported video format. Valid extensions: $($AllowedExtensions -join ', ')"
        exit
    }
}

# 3. Time and Duration Validation via FFprobe
try {
    $videoDurationSecs = & $ffprobePath -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 $file
    $videoDurationSecs = [double]$videoDurationSecs
} catch {
    Write-Warning "Failed to read video duration using FFprobe. The file might be corrupted."
    exit
}

try {
    $timeSpan = [TimeSpan]::Parse($time)
    $requestedTimeSecs = $timeSpan.TotalSeconds
} catch {
    Write-Warning "The specified time format ($time) is invalid. Use the HH:MM:SS pattern."
    exit
}

if ($requestedTimeSecs -gt $videoDurationSecs) {
    Write-Host ""
    Write-Host "=======================================================================" -ForegroundColor Red
    Write-Host " TIME VALIDATION ERROR:" -ForegroundColor Red
    Write-Host " The requested time ($time) exceeds the video length." -ForegroundColor White
    Write-Host " Requested Time: $requestedTimeSecs seconds." -ForegroundColor Gray
    Write-Host " Video Duration: $videoDurationSecs seconds." -ForegroundColor Yellow
    Write-Host "=======================================================================" -ForegroundColor Red
    exit
}

if ($secs -le 0) {
    Write-Warning "Extraction duration (-secs) must be greater than 0."
    exit
}

# ==========================================================================
# EXECUTION
# ==========================================================================
Write-Host "Video validated successfully! Starting extraction..." -ForegroundColor Green
$outputPattern = Join-Path $outputDir "frame_%03d.png"
$ffmpegArgs = @("-ss", "$time", "-i", "$file", "-t", "$secs", "-f", "image2", $outputPattern)

& $ffmpegPath $ffmpegArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host "Frames saved successfully to: $outputDir" -ForegroundColor Green
} else {
    Write-Warning "FFmpeg encountered an error during frame extraction."
}
