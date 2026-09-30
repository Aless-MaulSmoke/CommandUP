# ==========================================================================
# 2. FUNÇÃO DE INICIALIZAÇÃO DO AMBIENTE E SHADER (GLOBAL)
# ==========================================================================
function Initialize-GlobalPipeline {
    param (
        [PSCustomObject]$Config
    )

    # Define os caminhos das ferramentas e estruturas do ambiente
    $paths = [PSCustomObject]@{
		ffmpeg          = Join-Path $Global:CUP_ROOT "progs\ffmpeg\bin\ffmpeg.exe"
		ffprobe         = Join-Path $Global:CUP_ROOT "progs\ffmpeg\bin\ffprobe.exe"
		mediainfo       = Join-Path $Global:CUP_ROOT "progs\mediainfo\MediaInfo.exe"
		logpath         = Join-Path $Global:CUP_ROOT "log"
		shader          = Join-Path $Global:CUP_ROOT "shaders\fsr.glsl"
		shaderFFmpeg    = ""
    }
	
	# Adiciona ffmpeg ao path do sistema
	$env:Path = "$(Join-Path $Global:CUP_ROOT "ffmpeg\bin");$env:Path"
	
    $pipeline = [PSCustomObject]@{
		gpuName         = ""
		gpuVendor       = ""
		gpuColorFix     = $false
		gpuVulkanArgs   = ""
		vulkan_id       = 0
		codec8BitsSupp  = $false
		codec10BitsSupp = $false
		qp_i            = 0
		qp_p            = 0
		verboseArgs     = @()
    }
	
	# Define o codec correto e os perfis de qualidade CRF universais
	$crfProfiles = @{ "LOW" = 24; "MED" = 19; "BIG" = 14 }
	$pipeline.qp_i = $crfProfiles[$Config.quality] # Reutilizando a variável qp_i para guardar o QP/CRF base

	# Dicionário de mapeamento de codecs, perfis e formatos para teste hevc por Fabricante
	$Global:vendorCodecs = @{
		"AMD"    = @{ "AVC" = "h264_amf";   "HEVC" = "hevc_amf";   "PROBE_10BIT" = "yuv420p10le" }
		"NVIDIA" = @{ "AVC" = "h264_nvenc"; "HEVC" = "hevc_nvenc"; "PROBE_10BIT" = "p010le"       }
		"INTEL"  = @{ "AVC" = "h264_qsv";   "HEVC" = "hevc_qsv";   "PROBE_10BIT" = "p010le"       }
		"CPU"    = @{ "AVC" = "libx264";    "HEVC" = "libx265";    "PROBE_10BIT" = "yuv420p10le" }
	}
	
	$Global:vendorArgs = @{
		"AMD"    = @("-rc", "cqp", "-qp_i", $pipeline.qp_i, "-qp_p", ($pipeline.qp_i + 2))
		"NVIDIA" = @("-rc", "constqp", "-qp", $pipeline.qp_i)
		"INTEL"  = @("-global_quality", $pipeline.qp_i)
		"CPU"    = @("-crf", $pipeline.qp_i, "-preset", "ultrafast")
	}

	try {
		# ================
		# IDENTIFICA A VCARD PELO WINDOWS

		# Captura o nome real da vcard via gpu_id e os dados de identificação do Windows
		$todasGpusWin = Get-CimInstance Win32_VideoController
		$gpuAlvoWin = $todasGpusWin[$Config.gpu_id]
		
		$pipeline.gpuName = $gpuAlvoWin.Name
		$pipeline.gpuVendor = $pipeline.gpuName.ToUpper()
		$pnpIDWindows = $gpuAlvoWin.PNPDeviceID.ToUpper() # Força caixa alta para o Regex de casamento de IDs

		# Extrai blocos VEN e DEV do PNPDeviceID do Windows
		if ($pnpIDWindows -match 'VEN_(?<ven>[0-9A-F]{4})&DEV_(?<dev>[0-9A-F]{4})') {
			$chipIdentificacaoWindows = "$($Matches['ven']):$($Matches['dev'])".ToLower() # Formato padrão: "1002:699f"
		} else {
			throw "It's not possible to extract hardware IDs (Vendor/Device) of PNPDeviceID from Windows."
		}

		# Extrai o Barramento Físico direto do PNPDeviceID do Windows para o formato estrito do Teste Válido (ex: 0000:01:00:0)
		$winBus = 0; $winDev = 0; $winFun = 0
		if ($pnpIDWindows -match 'SUBSYS_[0-9A-F]+\\.*?&(?<bus>[0-9A-F]+)&(?<devfun>[0-9A-F]+)') {
			$winBus = [System.Convert]::ToInt32($Matches['bus'], 16)
			$winAddress = [System.Convert]::ToInt32($Matches['devfun'], 16)
			$winDev = ($winAddress -shr 16) -band 0xFFFF
			$winFun = $winAddress -band 0xFFFF
		} else {
			$winBus = 1; $winDev = 0; $winFun = 0
		}
		$pciAlvoWindows = "0000:{0:x2}:{1:x2}:{2:x1}" -f $winBus, $winDev, $winFun

		# ================
		# IDENTIFICA A VCARD PELO VULKAN

		# Coleta a listagem global de vcards gerada pelo Vulkan do FFmpeg
		$gpuTexto = & $paths.ffmpeg -hide_banner -v verbose -init_hw_device vulkan 2>&1 | Out-String
		$gpuBloco = if ($gpuTexto -match '(?ms)GPU listing:(?<bloco>.*?)\]\s+(?:Device\s+\d+\s+selected|Queue families):') { $Matches['bloco'] }
		
		# Popula a lista inicial com IDs e Nomes reais que o Vulkan listou
		$vulkanListing = [regex]::Matches($gpuBloco, '(?m)^\s*\[[^\]]+?\]\s+(?<id>\d+):\s+(?<name>.+?)(?=\s+\(|\r|\n)') | ForEach-Object { 
			[PSCustomObject]@{ 
				ID   = [int]$_.Groups['id'].Value
				Name = $_.Groups['name'].Value.Trim()
			} 
		}

		#debug
		if ($Config.debug -eq $true) {
			Write-Host "[ randon gpu list with randon id ] $($vulkanListing | Format-Table | Out-String) `n" -ForegroundColor Yellow
		}

		# Loop para varrer lista de vcards em busca de nomes iguais 
		$matchesPorNome = [System.Collections.Generic.List[PSCustomObject]]::new()

		foreach ($gpuVulkan in $vulkanListing) {
			if ($pipeline.gpuName -match [regex]::Escape($gpuVulkan.Name) -or $gpuVulkan.Name -match [regex]::Escape($pipeline.gpuName)) {
				$matchesPorNome.Add($gpuVulkan)
			}
		}

		# Inicia lógica de localização 
		$pipeline.vulkan_id = $null

		# Se apenas uma ocorrência for encontrada, ela é a placa focada
		if ($matchesPorNome.Count -eq 1) {
			$pipeline.vulkan_id = [int]$matchesPorNome[0].ID
			
		}
		# ou Se mais de uma ocorrência for encontrada, temos um cluster/SLI de placas idênticas
		elseif ($matchesPorNome.Count -gt 1) {
			
			# Loop apenas nas vcards com o mesmo nome da vcard foco
			foreach ($gpuDuplicada in $matchesPorNome) {
				
				# Mini-execução ffmpeg com libplacebo no ID da duplicidade
				$logDispositivo = & $paths.ffmpeg -hide_banner -v verbose -init_hw_device "vulkan=vk:$($gpuDuplicada.ID)" -f lavfi -i nullsrc=s=16x16:d=1 -vf "hwupload,libplacebo=w=16:h=16" -vframes 1 -f null - 2>&1 | Out-String

				# Inicializa as variáveis de validação do log
				$pciVulkan   = "Not found"
				$devIdVulkan = "Not found"

				# Captura o Endereço PCI físico (ex: 0000:01:00:0)
				if ($logDispositivo -match '(?m)PCI:\s*(?<pci>[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}:[0-9a-fA-F]{1})') {
					$pciVulkan = $Matches['pci'].Trim().ToLower()
				}

				# Captura o Device ID do chip (ex: 1002:699f)
				if ($logDispositivo -match '(?m)Device ID:\s*(?<devid>[0-9a-fA-F]{4}:[0-9a-fA-F]{4})') {
					$devIdVulkan = $Matches['devid'].Trim().ToLower()
				}
				
				# Comparação final se Device ID e o Endereço PCI extraidos combam com o Windows
				if ($devIdVulkan -eq $chipIdentificacaoWindows.ToLower() -and $pciVulkan -eq $pciAlvoWindows.ToLower()) {
					$pipeline.vulkan_id = [int]$gpuDuplicada.ID
					break # Para ao encontrar a correpondencia correta
				}
			}
		}
		# Cenário de Falha Crítica: Nenhuma ocorrência por texto bateu com o Windows
		else {
			throw "GPU listed by Vulkan doesn't match the name in Windows: ($($pipeline.gpuName))."
		}
		
		# Validação de segurança final da pipeline
		if ($null -eq $pipeline.vulkan_id) {
			throw "A pipeline failed to validate the target GPU ID with the Vulkan subsystem."
		}

		#debug
		if ($Config.debug -eq $true) {
			Write-Host "GPU name on Windows : $($pipeline.gpuName)" -ForegroundColor Yellow
			Write-Host "Chip Subscription   : $chipIdentificacaoWindows" -ForegroundColor Yellow
			Write-Host "Assigned Vulkan ID  : $($pipeline.vulkan_id)" -ForegroundColor Yellow
			Write-Host "------------------------------------------------" -ForegroundColor Yellow
		}

	} catch {
		Write-Warning "Critical failure: $_"
		exit
	}

	# Setagem de gpu por fabricante
	if ($pipeline.gpuVendor -match "AMD" -or $pipeline.gpuVendor -match "RADEON") {
		if ($pipeline.gpuVendor -match "Vega") { $pipeline.gpuColorFix = $true }
		$pipeline.gpuVendor = "AMD"
	} elseif ($pipeline.gpuVendor -match "NVIDIA" -or $pipeline.gpuVendor -match "GEFORCE") {
		$pipeline.gpuVendor = "NVIDIA"
		$pipeline.gpuVulkanArgs = ",disable_multiplane=1"
	} elseif ($pipeline.gpuVendor -match "INTEL") {
		$pipeline.gpuVendor = "INTEL"
	} else {
		$pipeline.gpuVendor = "CPU"
	}
	
	# Seleciona a gpu caso seja simulada
	if ($Config.simulate -ne "NONE" -and $Config.simulate -ne "") {
		$pipeline.gpuName = $pipeline.gpuName + " |Simulated $($Config.simulate) Card"
		$pipeline.gpuVendor = $Config.simulate
	}
	
	# Verifica a existencia de encoders na vcard
	if ($pipeline.gpuVendor -ne "CPU") {
		$codecAlvo = $vendorCodecs[$pipeline.gpuVendor][$Config.codec]

		# Realiza teste fisico para comprovar suporte
		if (Test-Path $paths.ffmpeg) {
			
			$codecFalhas = "Error while opening encoder|not supported|Conversion failed|Incompatible pixel format|auto-selecting"

			# Testa 8bits
			$probe8Bits = "yuv420p"
			$args8 = @("-init_hw_device", "vulkan=vk:$($Config.gpu_id)", "-f", "lavfi", "-i", "nullsrc=s=1280x720:d=1", "-c:v", $codecAlvo, "-pix_fmt", $probe8Bits, "-f", "null", "-")
			$res8  = & $paths.ffmpeg -hide_banner $args8 2>&1 | Out-String

			if ($res8 -notmatch $codecFalhas) {
				$pipeline.codec8BitsSupp = $true
			}
			
			if ($Config.codec.ToUpper() -eq "HEVC" -and $pipeline.codec8BitsSupp) {

				# Testa 10bits
				$probe10Bits = $vendorCodecs[$pipeline.gpuVendor]["PROBE_10BIT"]
				$args10 = @("-init_hw_device", "vulkan=vk:$($Config.gpu_id)", "-f", "lavfi", "-i", "nullsrc=s=1280x720:d=1", "-c:v", $codecAlvo, "-pix_fmt", $probe10Bits, "-f", "null", "-")
				$res10  = & $paths.ffmpeg -hide_banner $args10 2>&1 | Out-String

				if ($res10 -notmatch $codecFalhas) {
					$pipeline.codec10BitsSupp = $true
				}
			}

		}

	} else { 
		$pipeline.codec8BitsSupp = $true
		$pipeline.codec10BitsSupp = $true
	}
	
	# Seta codec globalmente
	$Global:SelectedCodec = $Global:vendorCodecs[$pipeline.gpuVendor][$Config.codec]
	$Global:CodecArgs     = $Global:vendorArgs[$pipeline.gpuVendor]
	
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

		if (Test-Path $paths.shader) {
			$linhasShader = Get-Content $paths.shader
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
			Set-Content $paths.shader -Value $linhasShader -Encoding UTF8
		}
	}

    # Prepara o caminho do shader formatado para o libplacebo
    $paths.shaderFFmpeg = $paths.shader.Replace("\", "/").Replace(":", "\:")
	
	# Cria a lista que vai guardar o histórico de todos os vídeos processados na sessão
	$Global:SessionHistory = @()

	#debug
	if ($Config.debug -eq $true) {
		Write-Host "`n[ pipeline ] $($pipeline) `n" -ForegroundColor Yellow
	}
	
	# mescla objetos paths e pipeline para retorno
	$pipelineReturn = @{}
    foreach ($p in $paths.psobject.properties) { $pipelineReturn[$p.Name] = $p.Value }
    foreach ($p in $pipeline.psobject.properties) { $pipelineReturn[$p.Name] = $p.Value }

    return [PSCustomObject]$pipelineReturn
	
}
