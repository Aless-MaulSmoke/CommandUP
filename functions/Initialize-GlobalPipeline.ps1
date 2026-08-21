 # ==========================================================================
# 2. FUNÇÃO DE INICIALIZAÇÃO DO AMBIENTE E SHADER (GLOBAL)
# ==========================================================================
function Initialize-GlobalPipeline {
    param (
        [PSCustomObject]$Config
    )

    # Define os caminhos das ferramentas e estruturas do ambiente
    $pipeline = [PSCustomObject]@{
		ffmpeg          = Join-Path $Global:CUP_ROOT "ffmpeg\bin\ffmpeg.exe"
		ffprobe         = Join-Path $Global:CUP_ROOT "ffmpeg\bin\ffprobe.exe"
		logpath         = Join-Path $Global:CUP_ROOT "log"
		shader          = Join-Path $Global:CUP_ROOT "shaders\fsr.glsl"
		shaderFFmpeg    = ""
		gpuName         = ""
		gpuVendor       = ""
		gpuColorFix     = $false
		gpuVulkanArgs   = ""
		codec8BitsSupp  = $false
		codec10BitsSupp = $false
		qp_i            = 0
		qp_p            = 0
		verboseArgs     = @()
		interpolate     = $Config.interpolate
    }
	
	# Define o codec correto e os perfis de qualidade CRF universais
	$crfProfiles = @{ "LOW" = 24; "MED" = 19; "BIG" = 14 }
	$qualidadeAlvo = $Config.quality.ToUpper()
	$pipeline.qp_i = $crfProfiles[$qualidadeAlvo] # Reutilizando a variável qp_i para guardar o QP/CRF base

	# Dicionário de mapeamento de codecs, perfis e formatos para teste hevc por Fabricante
	$vendorCodecs = @{
		"AMD"    = @{ "AVC" = "h264_amf";   "HEVC" = "hevc_amf";   "PROBE_10BIT" = "yuv420p10le" }
		"NVIDIA" = @{ "AVC" = "h264_nvenc"; "HEVC" = "hevc_nvenc"; "PROBE_10BIT" = "p010le"       }
		"INTEL"  = @{ "AVC" = "h264_qsv";   "HEVC" = "hevc_qsv";   "PROBE_10BIT" = "p010le"       }
		"CPU"    = @{ "AVC" = "libx264";    "HEVC" = "libx265";    "PROBE_10BIT" = "yuv420p10le" }
	}
	
	$vendorArgs = @{
		"AMD"    = @("-rc", "cqp", "-qp_i", $pipeline.qp_i, "-qp_p", ($pipeline.qp_i + 2))
		"NVIDIA" = @("-rc", "constqp", "-qp", $pipeline.qp_i)
		"INTEL"  = @("-global_quality", $pipeline.qp_i)
		"CPU"    = @("-crf", $pipeline.qp_i, "-preset", "ultrafast")
	}

	try {
		
		# Captura a vcard de acordo com o parametro gpu_id via Win32
		$pipeline.gpuName = (Get-CimInstance Win32_VideoController)[$Config.gpu_id].Name
		$pipeline.gpuVendor = $pipeline.gpuName.ToUpper()
		
		# Prepara lista de vcards com suas ids aleatórias via ffmpeg
		$gpuTexto = & $pipeline.ffmpeg -hide_banner -v verbose -init_hw_device vulkan 2>&1 | Out-String
		$gpuBloco = if ($gpuTexto -match '(?ms)GPU listing:(?<bloco>.*?)Device') { $Matches['bloco'] }
		$vulkanListing = [regex]::Matches($gpuBloco, '(?m)^\s*\[Vulkan\s+@\s+\w+\]\s+(?<id>\d+):\s+(?<name>.+?)(?=\s\()') | ForEach-Object { [PSCustomObject]@{ ID = $_.Groups['id'].Value; Name = $_.Groups['name'].Value.Trim() } }
		
		# Redefine gpu_id usando id real vulkan
		$vcardAlvo = $vulkanListing | Where-Object { $pipeline.gpuName -match $_.Name -or $_.Name -match $pipeline.gpuName.Split(' ')[0] } | Select-Object -First 1
		$Config.gpu_id = [int]$vcardAlvo.ID

		#debug
		if ($Config.debug -eq $true -or $Config.debug -eq "true") {
			Write-Host "[ randon gpu list with randon id ] $($vulkanListing | Format-Table | Out-String) `n" -ForegroundColor Yellow
			Write-Host "[ Selected: ] $($vcardAlvo) `n" -ForegroundColor Yellow
		}
		
		# Seleciona a gpu caso seja simulada
		if ($Config.simulate_gpu -ne "NONE" -and $Config.simulate_gpu -ne "") {
			$pipeline.gpuName = $pipeline.gpuName + " |Simulated $($Config.simulate_gpu) Card"
			$pipeline.gpuVendor = $Config.simulate_gpu
		}
	
	} catch {
		Write-Warning "gpu_id: $($Config.gpu_id) don't exists. Please update parameter to accept value."
		exit
    }

	if ($pipeline.gpuVendor -match "AMD" -or $pipeline.gpuVendor -match "RADEON") {
		if ($pipeline.gpuVendor -match "Vega") { $pipeline.gpuColorFix = $true }
		$pipeline.gpuVendor = "AMD"
	} 
	elseif ($pipeline.gpuVendor -match "NVIDIA" -or $pipeline.gpuVendor -match "GEFORCE") {
		$pipeline.gpuVendor = "NVIDIA"
		$pipeline.gpuVulkanArgs = ",disable_multiplane=1"
	} 
	elseif ($pipeline.gpuVendor -match "INTEL") {
		$pipeline.gpuVendor = "INTEL"
	} 
	else {
		# Em ultimo caso H.264 rodando diretamente na CPU
		$pipeline.gpuVendor = "CPU"
	}
	
	# Verifica a existencia de encoders na vcard
	if ($pipeline.gpuVendor -ne "CPU") {
		$codecAlvo = $vendorCodecs[$pipeline.gpuVendor][$Config.codec]

		# Realiza teste fisico para comprovar suporte
		if (Test-Path $pipeline.ffmpeg) {
			
			# Testa 8bits
			$probe8Bits = "yuv420p"
			$args8 = @("-f", "lavfi", "-i", "nullsrc=s=1280x720:d=1", "-c:v", $codecAlvo, "-gpu", $Config.gpu_id, "-pix_fmt", $probe8Bits, "-f", "null", "-")
			$res8  = & $pipeline.ffmpeg -hide_banner $args8 2>&1 | Out-String

			if ($res8 -notmatch "Error while opening encoder" -and $res8 -notmatch "not supported") {
				$pipeline.codec8BitsSupp = $true
			}
			
			if ($Config.codec.ToUpper() -eq "HEVC" -and $pipeline.codec8BitsSupp) {

				# Testa 10bits
				$probe10Bits = $vendorCodecs[$pipeline.gpuVendor]["PROBE_10BIT"]
				$args10 = @("-f", "lavfi", "-i", "nullsrc=s=1280x720:d=1", "-c:v", $codecAlvo, "-gpu", $Config.gpu_id, "-pix_fmt", $probe10Bits, "-f", "null", "-")
				$res10  = & $pipeline.ffmpeg -hide_banner $args10 2>&1 | Out-String

				if ($res10 -notmatch "Conversion failed" -and $res10 -notmatch "not supported") {
					$pipeline.codec10BitsSupp = $true
				}

			}

		}
		
		# Se a GPU falhar no teste básico de 8-bit, rebaixa para a CPU
		if (-not $pipeline.codec8BitsSupp) {
			$pipeline.gpuName   = "$($pipeline.gpuName) (don't encoder support |using CPU)"
			$pipeline.gpuVendor = "CPU"
		}
			
	}
	
	# Seta codec globalmente
	$Global:SelectedCodec = $vendorCodecs[$pipeline.gpuVendor][$Config.codec]
	$Global:CodecArgs     = $vendorArgs[$pipeline.gpuVendor]
	
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
