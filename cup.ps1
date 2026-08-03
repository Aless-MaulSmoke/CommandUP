# ==========================================================================
# ------------------------------
#
#   Command UP (cup) Pipeline (v1.0.0)
#   An automated pipeline for high-performance video upscaling via CLI.
#
#   cup.ps1 (Powershell script)
#   2026/08/03
#   by Aless (MaulSmoke)
#
#   A modular script designed to orchestrate lightweight, hardware-accelerated 
#   Vulkan workflows for video post-processing without heavy AI dependencies.
#
#  -file "string"
#   The absolute or relative path to the single input video file.
#
#  -folder "string"
#   The path to a target directory containing multiple videos for batch processing.
#
#  -scale "string"
#   The target resolution (e.g., "1920x1080") or decimal multiplier factor (e.g., 1.5).
#
#  -fps number
#   The target frame rate for the final output video smoothness (e.g., 60).
#
#  -interpolate "string"
#   The frame mixing algorithm: "none", "oversample", "mitchell_clamp", or "linear".
#
#  -quality "string"
#   The compression profile and file size weight: "LOW", "MED", or "BIG".
#
#  -sharpness number
#   The edge crispness applied by FSR, ranging from 0 to 10 (Default is 5).
#
#  -hdr
#   Switch flag to force the FSR pass into the PQ color space pipeline (HDR10 source only).
#
#  -shutdown
#   Switch flag to trigger a safe 30-second system power-off countdown after execution.
#
#  -DRAG_CONFIG
#   Switch flag to automatically read and load parameters directly from DRAG_CONFIG.txt.
#
#  -hud_port
#   Defines the TCP port used to real-time HUD progress interface between ffmpeg and cup.
#
#  -verbose
#   Switch flag to toggle detailed real-time rendering logs (Verbose mode).
#
#  -gpu_id
#   If you have more than one video card, specify which one you want to use via the video card ID number.
#
# ------------------------------

param(
    [string]$file,
    [string]$folder,
    [string]$scale,
    [int]$fps,
	[string]$interpolate = "none",
    [string]$quality = "MED",
	[System.Nullable[int]]$sharpness,
	[switch]$hdr,
	[switch]$shutdown,
	[switch]$DRAG_CONFIG,
    [int]$hud_port,
	[switch]$verbose,
	[int]$gpu_id
)

# =====================================================================
# SETA PASTA ROOT E CARREGA FUNCTIONS
# =====================================================================
$Global:CUP_ROOT = $PSScriptRoot
$pastaFunctions = Join-Path $Global:CUP_ROOT "functions"

# Importação das functions do sistema (Dot-Sourcing)
if (Test-Path $pastaFunctions) {
    . (Join-Path $pastaFunctions "Get-ScriptConfig.ps1")
    . (Join-Path $pastaFunctions "Initialize-GlobalPipeline.ps1")
    . (Join-Path $pastaFunctions "Get-VideoQueue.ps1")
    . (Join-Path $pastaFunctions "Get-VideoMetadataAndValidate.ps1")
    . (Join-Path $pastaFunctions "Invoke-VideoPipeline.ps1")
    . (Join-Path $pastaFunctions "Show-VideoStatus.ps1")
    . (Join-Path $pastaFunctions "Out-GlobalSummary.ps1")
} else {
    Write-Error "[CRITICAL] The 'functions' folder was not found in the root of the Engine!"
    Exit 1
}

# ==========================================================================
# BLOCO PRINCIPAL DE EXECUÇÃO
# ==========================================================================

# Captura e unifica as configurações gerais
$Config = Get-ScriptConfig -BoundParameters $PSBoundParameters

# Inicializa o ambiente global e shaders
$Pipeline = Initialize-GlobalPipeline -Config $Config

# Cria a fila uniforme de processamento
$FilaTrabalho = Get-VideoQueue -Config $Config

# Inicializa o objeto acumulador para as Estatísticas Gerais
$StatusGeral = [PSCustomObject]@{
    TotalSucesso         = 0
    TotalPulados         = 0
    TotalFalhas          = 0
    TempoTotalSegundos   = 0.0
    TamanhoTotalBytes    = [long]0
    DuracaoTotalVideos   = 0.0
}

Write-Host "`nStarting to process the queue..." -ForegroundColor Cyan

# O Loop de Processamento processando cada arquivo de forma isolada
foreach ($VideoAtual in $FilaTrabalho) {
    
    # Extração de Metadados e Validação de Redundância por Arquivo
    $Metadata = Get-VideoMetadataAndValidate -VideoPath $VideoAtual -Config $Config -Pipeline $Pipeline
    
    # Se o arquivo foi pulado por redundância ou falha no FFprobe
	if ($Metadata.SkipVideo) {
		$Global:SessionHistory += $Metadata
		Show-VideoStatus -Result $Metadata -StatusAcumulado ([ref]$StatusGeral)
		continue 
	}

    # Execução da Pipeline do FFmpeg para o vídeo atual
	$Global:LastProcessResult = Invoke-VideoPipeline -VideoPath $VideoAtual -Config $Config -Pipeline $Pipeline -Metadata $Metadata
	$ResultadoProcesso = $Global:LastProcessResult
	$Global:SessionHistory += $Global:LastProcessResult
    
    # Exibição de Conclusão em Linha Única e Acumulação de Métricas
    Show-VideoStatus -Result $ResultadoProcesso -StatusAcumulado ([ref]$StatusGeral)
}

# Exibição da Estatística Geral de todos os arquivos e Gatilho de Desligamento
Out-GlobalSummary -StatusAcumulado $StatusGeral -ShutdownAtivo $Config.shutdown -Pipeline $Pipeline

