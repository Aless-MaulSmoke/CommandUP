# ==========================================================================
# 4. CONFIGURAÇÃO DE METADADOS INDIVIDUAL E VALIDAÇÃO DE REDUNDÂNCIA
# ==========================================================================
function Get-VideoMetadataAndValidate {
param (
    [string]$VideoPath,
    [PSCustomObject]$Config,
    [PSCustomObject]$Pipeline
)

    # 1. Extração de Metadados via FFprobe
	$ffprobeArgs = @(
		"-v", 
		"error",
		"-select_streams", 
		"v:0",
		"-show_entries", 
		"stream=width,height,r_frame_rate,pix_fmt,color_space,color_range:format=duration",
		"-of", 
		"csv=p=0",
		$VideoPath
	)	

	try {
		$probeOutput = & $Pipeline.ffprobe $ffprobeArgs 2>$null
		if ($null -eq $probeOutput -or $probeOutput.Trim() -eq "") {
			throw "Could not read the video properties."
		}

		# Substitui quebras de linha por vírgulas e remove espaços, criando uma linha única limpa
		$textoUnificado = $probeOutput.Trim() -replace "`r", "" -replace "`n", ","
		
		# Transforma em um Array Real de elementos separados (Força a tipagem de lista do PowerShell)
		[string[]]$partesValidas = $textoUnificado -split ','

		# Mapeamento matemático direto pelos índices reais da lista:
		$wOriginal   = [int]$partesValidas[0]
		$hOriginal   = [int]$partesValidas[1]
		$pixFormat   = [string]$partesValidas[2]
		$colorRange  = [string]$partesValidas[3]
		$colorSpace  = [string]$partesValidas[4]
		$fpsRaw      = [string]$partesValidas[5]
		$duracaoSegundos = [double]$partesValidas[6]
		$fpsParts    = $fpsRaw -split '/'
		$fpsOriginal = [math]::Round(([double]$fpsParts[0] / [double]$fpsParts[1]), 2)

		# Normalização estrita para as diretrizes das APIs scale_vulkan e libplacebo
		if ($colorRange -eq "tv") { $colorRange = "limited" }
		if ($colorRange -eq "pc") { $colorRange = "full" }

		if ([string]::IsNullOrEmpty($pixFormat)  -or $pixFormat  -eq "unknown") { $pixFormat  = "nv12" }
		if ([string]::IsNullOrEmpty($colorSpace) -or $colorSpace -eq "unknown") { $colorSpace = "bt709" }
		
	} catch {
        return [PSCustomObject]@{
            Success = $false
            SkipVideo = $true
            ErrorMessage = "Failed to extract metadata via FFprobe. File is corrupted or incompatible."
            NomeArquivo = [System.IO.Path]::GetFileName($VideoPath)
        }
    }
	
    $nomeArquivo = [System.IO.Path]::GetFileName($VideoPath)
    $widthOut    = $wOriginal
    $heightOut   = $hOriginal
    $fpsOut      = $fpsOriginal

    if ($Config.scale) {
		# Converte para string para garantir que métodos de texto funcionem se o terminal passar número puro
		$scaleStr = [string]$Config.scale

		# Aceita inteiros ou decimais (com ponto/vírgula) e o 'x' opcional no final
		if ($scaleStr -match '^(\d+[\.,]?\d*)x?$') {
			# Limpa o 'x' se houver e padroniza o ponto decimal para o cálculo numérico [1.1]
			$fatorLimpo    = $Matches[1].Replace(',', '.')
			$multiplicador = [double]$fatorLimpo
			
			$widthOut      = [int]($wOriginal * $multiplicador)
			$heightOut     = [int]($hOriginal * $multiplicador)
		} elseif ($scaleStr -match '^\d+x\d+$') {
            $resParts  = $scaleStr -split 'x'
            $widthOut  = [int]$resParts[0]
            $heightOut = [int]$resParts[1]
        }
    }

    if ($Config.fps) {
        $fpsOut = [double]$Config.fps
    }

    # Validação de Redundância Individual
    $isResolutionRedundant = ($widthOut -eq $wOriginal -and $heightOut -eq $hOriginal)
    $isFpsRedundant        = ([math]::Abs($fpsOut - $fpsOriginal) -lt 0.01)

    if ($isResolutionRedundant -and $isFpsRedundant) {
        return [PSCustomObject]@{
            Success     = $true
            SkipVideo   = $true
            NomeArquivo = $nomeArquivo
            Reason      = "Original resolution: $($wOriginal)x$($hOriginal)/$($fpsOriginal)fps, are already identical to the requested targets."
        }
    }
	
    return [PSCustomObject]@{
		Success         = $true
		SkipVideo       = $false
		NomeArquivo     = $nomeArquivo
		wOriginal       = $wOriginal
		hOriginal       = $hOriginal
		pixFormat       = $pixFormat
		colorSpace      = $colorSpace
		colorRange      = $colorRange
		fpsOriginal     = $fpsOriginal
		widthOut        = $widthOut
		heightOut       = $heightOut
		fpsOut          = $fpsOut
		duracaoSegundos = $duracaoSegundos 
		skipFSR         = $isResolutionRedundant
		skipIFS         = $isFpsRedundant
    }
}
