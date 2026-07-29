# ==========================================================================
# 2. FUNÇÃO DE INICIALIZAÇÃO DO AMBIENTE E SHADER (GLOBAL)
# ==========================================================================
function Initialize-GlobalPipeline {
    param (
        [PSCustomObject]$Config
    )

    # Define os caminhos das ferramentas e estruturas do ambiente
    $pipeline = [PSCustomObject]@{
		ffmpeg       = Join-Path $Global:FSRIFS_ROOT "ffmpeg\bin\ffmpeg.exe"
		ffprobe      = Join-Path $Global:FSRIFS_ROOT "ffmpeg\bin\ffprobe.exe"
		logpath      = Join-Path $Global:FSRIFS_ROOT "log"
		shader       = Join-Path $Global:FSRIFS_ROOT "shaders\fsr.glsl"
		shaderFFmpeg = ""
		gpuName      = ""
		gpuColorFix  = $false
		qp_i         = 0
		qp_p         = 0
		verboseArgs  = @()
		interpolate  = $Config.interpolate
    }

	# Faz a consulta de hardware
	$pipeline.gpuName = (Get-CimInstance Win32_VideoController | Select-Object -First 1).Name
	$gpuVendor = $pipeline.gpuName.ToUpper()

	# Define o codec correto e os perfis de qualidade CRF universais
	$crfProfiles = @{ "LOW" = 24; "MED" = 19; "BIG" = 14 }
	$qualidadeAlvo = $Config.quality.ToUpper()
	$pipeline.qp_i = $crfProfiles[$qualidadeAlvo] # Reutilizando a variável qp_i para guardar o QP/CRF base

	if ($gpuVendor -match "AMD" -or $gpuVendor -match "RADEON") {
		$Global:SelectedCodec = "h264_amf"
		$Global:CodecArgs = @("-rc", "cqp", "-qp_i", $pipeline.qp_i, "-qp_p", ($pipeline.qp_i + 2))

		if ($gpuVendor -match "Vega") { $pipeline.gpuColorFix = $true }
	} 
	elseif ($gpuVendor -match "NVIDIA" -or $gpuVendor -match "GEFORCE") {
		$Global:SelectedCodec = "h264_nvenc"
		$Global:CodecArgs = @("-rc", "constqp", "-qp", $pipeline.qp_i)
	} 
	elseif ($gpuVendor -match "INTEL") {
		$Global:SelectedCodec = "h264_qsv"
		$Global:CodecArgs = @("-global_quality", $pipeline.qp_i)
	} 
	else {
		# Em ultimo caso H.264 rodando diretamente na CPU
		$Global:SelectedCodec = "libx264"
		$Global:CodecArgs = @("-crf", $pipeline.qp_i, "-preset", "ultrafast")
	}


    # Define argumentos verbose
    if ($Config.verbose) { 
		$pipeline.verboseArgs = @("-v", "verbose") 
	} else {
		$pipeline.verboseArgs = @("-v", "repeat+error", "-stats") 
	}

	# Aplicação GLOBAL da Nitidez (Sharpness) e suporte HDR diretamente no arquivo de shader
	if ($Config.scale) {
		# Se omitido no CLI/TXT, assume o valor padrão 5 conforme o script original
		$sharpnessValor = if ($null -ne $Config.sharpness) { $Config.sharpness } else { 5 }
		$clampedUserSharpness = [math]::Max(0, [math]::Min(10, $sharpnessValor)) / 10.0
		$fsrSharpness = 2.0 * (1.0 - $clampedUserSharpness)

		# Define o valor do FSR_PQ
		$fsrPQValor = if ($Config.HDR -eq $true -or $Config.HDR -eq "true") { 1 } else { 0 }

		if (Test-Path $pipeline.shader) {
			$linhasShader = Get-Content $pipeline.shader
			$novaLinhaSharpness = "#define SHARPNESS $($fsrSharpness.ToString('0.0', [System.Globalization.CultureInfo]::InvariantCulture))"
			$novaLinhaPQ = "#define FSR_PQ $fsrPQValor"
			
			for ($i = 0; $i -lt $linhasShader.Count; $i++) {
				# Substitui a linha de Sharpness
				if ($linhasShader[$i] -like "#define SHARPNESS*") {
					$linhasShader[$i] = $novaLinhaSharpness
				}
				# Substitui a linha de FSR_PQ
				elseif ($linhasShader[$i] -like "#define FSR_PQ*") {
					$linhasShader[$i] = $novaLinhaPQ
				}
			}
			Set-Content $pipeline.shader -Value $linhasShader -Encoding UTF8
		}
	}

    # Prepara o caminho do shader formatado para o libplacebo
    $pipeline.shaderFFmpeg = $pipeline.shader.Replace("\", "/").Replace(":", "\:")
	
	# Cria a lista que vai guardar o histórico de todos os vídeos processados na sessão
	$Global:SessionHistory = @()

    return $pipeline
}
