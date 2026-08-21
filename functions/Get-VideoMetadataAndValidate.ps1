# ==========================================================================
# 4. CONFIGURAÇÃO DE METADADOS INDIVIDUAL E VALIDAÇÃO DE REDUNDÂNCIA
# ==========================================================================
function Get-VideoMetadataAndValidate {
param (
    [string]$VideoPath,
    [PSCustomObject]$Config,
    [PSCustomObject]$Pipeline
)

	# formatos suportados e raw bits
	$tabelaFormatos = @{
		"yuvj420p"    = 8
		"yuv420p"     = 8
		"nv12"        = 8
		"yuvj444p"    = 8
		"yuv444p"     = 8
		"p010le"      = 10
		"p010"        = 10
		"yuv420p10le" = 10
		"yuv444p10le" = 10
	}

    # Extração de Metadados via FFprobe
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
	
	[int]$bitsOutput = 0
	[bool]$bitsDowngrade = $false

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

		if ($colorRange -eq "tv") { $colorRange = "limited" }
		if ($colorRange -eq "pc") { $colorRange = "full" }

		if ($tabelaFormatos.ContainsKey($pixFormat)) {
			[int]$bitsFormat = $tabelaFormatos[$pixFormat]
		} else {
			throw "Error: The video format '$pixFormat' is not certified or supported."
		}
		
		# Herda profundidade de bits do video original
		[int]$bitsOutput = $bitsFormat
		[bool]$bitsDowngrade = $false

		# Bloqueio Crítico caso tente gerar HDR com video de origem que não seja 10bits
		if (($Config.HDR -eq $true -or $Config.HDR -eq "true") -and ($bitsFormat -ne 10 -or $Config.codec.ToLower() -ne "hevc")) {
			return [PSCustomObject]@{
				Success     = $false
				SkipVideo   = $true
				Reason      = "Critical Error: HDR mode strictly requires a 10-bit HEVC source video."
				NomeArquivo = [System.IO.Path]::GetFileName($VideoPath)
			}
		}

		# Força downgrade para 8bits se hevc não tiver suporte a 10bits na vcard
		if ($bitsFormat -eq 10 -and $Config.codec.ToLower() -eq "hevc" -and $Pipeline.codec10BitsSupp -eq $false) {
			$bitsOutput = 8
			$bitsDowngrade = $true
		}
		
	} catch {
        return [PSCustomObject]@{
            Success = $false
            SkipVideo = $true
            Reason = "Failed to extract metadata via FFprobe. File is corrupted or incompatible."
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
		bitsFormat      = $bitsFormat
		bitsOutput      = $bitsOutput
		bitsDowngrade   = $bitsDowngrade
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
