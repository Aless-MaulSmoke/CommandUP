# ==========================================================================
# 1. FUNÇÃO DE CONFIGURAÇÃO E TRATAMENTO DE PARÂMETROS
# ==========================================================================
function Get-ScriptConfig {
    param (
        [System.Collections.IDictionary]$BoundParameters
    )
	
    $config = [PSCustomObject]@{
		file         = $null
		folder       = $null
		quality      = $null
		fps          = $null
		interpolate  = $null
		scale        = $null
		sharpness    = $null
		codec        = $null
		hdr          = $null
		shutdown     = $null
		drag_config  = $false
		hud_port     = $null
		verbose      = $null
		gpu_id       = $null
		simulate_gpu = $null
    }

	# Descobre a pasta raiz subindo um nível a partir da pasta 'functions'
	$caminhoDragConfig = Join-Path $Global:CUP_ROOT "DRAG_CONFIG.txt"
	$caminhoConfigIni  = Join-Path $Global:CUP_ROOT "CONFIG.ini"

	# Carrega parametros do config.ini
	if (Test-Path $caminhoConfigIni) {
		# Busca apenas as chaves necessárias no arquivo de texto
		$chavesNecessarias = @("HUD_PORT", "VERBOSE", "GPU_ID")
		$conteudoTxt = Get-Content $caminhoConfigIni

		foreach ($chave in $chavesNecessarias) {
			$linha = $conteudoTxt | Where-Object { $_ -match "^$chave\s*=" }
			if ($linha) {
				$propriedade, $valor = $linha.Split('=', 2)
				$config.$($chave.ToLower()) = $valor.Trim()
			}
		}
	} else {
		# Printa um aviso na tela caso o config não seja encontrado
		Write-Host "`n[CRITICAL ERROR] The config.ini file was not found in the root folder" -ForegroundColor Red
		Write-Host "Path searched:$($caminhoConfigIni)" -ForegroundColor Yellow
		exit
	}
	# Sobrescreve parametro caso tenha sido informado pelo usuario e converte as chaves para minusculo
	$propriedadesValidas = $BoundParameters.GetEnumerator() | Where-Object { $_.Key -ne 'BoundParameters' }
	foreach ($prop in $propriedadesValidas) {
		$config.$($prop.Key.ToLower()) = $prop.Value
	}

	# Verifica se o parâmetro de comando -drag_config foi informado pelo usuário
	$usaDragConfig = $BoundParameters.ContainsKey('drag_config') -and $BoundParameters['drag_config'] -eq $true

	if ($usaDragConfig) {
		
		if (Test-Path $caminhoDragConfig) {
			# Se encontrou o arquivo, carrega os dados normalmente
			if ($BoundParameters.ContainsKey('file'))   { $config.file   = $BoundParameters['file'] }
			if ($BoundParameters.ContainsKey('folder')) { $config.folder = $BoundParameters['folder'] }
			$config.drag_config = $true

			# Busca apenas as chaves necessárias no arquivo de texto
			$chavesNecessarias = @("QUALITY", "FPS", "INTERPOLATE", "SCALE", "SHARPNESS", "CODEC", "HDR", "SHUTDOWN")
			$conteudoTxt = Get-Content $caminhoDragConfig

			foreach ($chave in $chavesNecessarias) {
				$linha = $conteudoTxt | Where-Object { $_ -match "^$chave\s*=" }
				if ($linha) {
					$propriedade, $valor = $linha.Split('=', 2)
					$config.$($chave.ToLower()) = $valor.Trim()
				}
			}
		} else {
			# Printa um aviso na tela caso o config não seja encontrado
			Write-Host "`n[CRITICAL ERROR] The Drag_Config.txt file was not found in the root folder" -ForegroundColor Red
			Write-Host "Path searched:$caminhoDragConfig" -ForegroundColor Yellow
			exit
		}
	}
	
    # ==========================================================================
    # TRATAMENTO DE TIPOS, PADRONIZAÇÃO E VALIDAÇÃO FINAL
    # ==========================================================================
    
    # Catálogo de erros mapeado por IDs
    $catalogoErros = @{
		1 = "Ambiguity Error: Use ONLY '-file' OR '-folder', not both at the same time."
		2 = "No input specified. Use '-file' for a single video or '-folder' for batch processing."
		3 = "The 'quality' parameter only accepts one of these valid options: LOW | MED | BIG "
		4 = "The 'fps' parameter must be a valid number greater than 0."
		5 = "The 'interpolate' parameter only accepts one of these valid options: none | oversample | mitchell_clamp | linear "
		6 = "The 'scale' parameter should be a resolution, e.g., 1920x1080, or a scaling factor, e.g., 1.5"
		7 = "The 'sharpness' parameter only accepts numbers between 0 and 10"
		8 = "The 'hdr' parameter only accepts true or false."
		9 = "The 'shutdown' parameter only accepts true or false."
		10 = "The 'hud_port' the parameter must be a valid integer number."
		11 = "The 'verbose' parameter only accepts true or false."
		12 = "The 'gpu_id' parameter must be a valid number."
		13 = "The 'simulate_gpu' parameter only accepts one of these valid options: none | nvidia | amd | intel | cpu "
		14 = "The 'codec' parameter only accepts one of these valid options: avc | hevc "
		15 = "The 'hdr' parameter requires the codec to be hevc."
    }
    $errosEncontrados = @()

    # valida: FILE e FOLDER
    if ($config.file -and $config.folder) { $errosEncontrados += 1 }
    if (-not $config.file -and -not $config.folder) { $errosEncontrados += 2 }

    # valida: QUALITY
    if ($null -ne $config.quality) {
        $config.quality = [string]$config.quality.ToString().ToUpper()
        if ($config.quality -notin @("LOW", "MED", "BIG")) { $errosEncontrados += 3 }
    } else {
        $config.quality = "MED" # Valor padrão de segurança agora atribui com sucesso!
    }

    # valida: FPS
    if ($null -ne $config.fps -and $config.fps -as [int]) {
        $config.fps = [int]$config.fps
        if ($config.fps -le 0) { $errosEncontrados += 4 }
    } else {
        if ($null -eq $config.fps) { $config.fps = 0 } else { $errosEncontrados += 4 }
    }

    # valida: INTERPOLATE
    if ($null -ne $config.interpolate) {
        $config.interpolate = [string]$config.interpolate.ToString().ToLower()
        $interpolacoesValidas = @("none", "oversample", "mitchell_clamp", "linear")
        if ($config.interpolate -notin $interpolacoesValidas) { $errosEncontrados += 5 }
    } else {
        $config.interpolate = "none"
    }

    # valida: SCALE
    if ($null -ne $config.scale -and $config.scale -ne "") {
        $config.scale = [string]$config.scale.ToString().Trim().ToLower()

        # Cenário 1: O usuário informou uma resolução (Ex: 1920x1080)
        if ($config.scale -match '^\d+x\d+$') {
            $largura, $altura = $config.scale.Split('x')
            if (($largura -as [int]) -and ($altura -as [int])) {
                if ([int]$largura -le 0 -or [int]$altura -le 0) {
                    $errosEncontrados += 6
                }
            } else {
                $errosEncontrados += 6
            }
        }
        # Cenário 2: O usuário informou um multiplicador decimal (Ex: 1.5 ou 2)
        elseif ($config.scale -as [double]) {
            $config.scale = [double]$config.scale
            if ($config.scale -le 0) {
                $errosEncontrados += 6
            }
        }
        # Cenário 3: Texto inválido que não encaixa em nenhum dos padrões
        else {
            $errosEncontrados += 6
        }
    } else {
        $config.scale = $null 
    }

	# valida: SHARPNESS
	if ($null -ne $config.sharpness -and $config.sharpness -ne "") {
		# Correção: Armazena o resultado da conversão e valida se ela é válida (não nula)
		$valorInt = $config.sharpness -as [int]
		if ($null -ne $valorInt) {
			$config.sharpness = $valorInt
			if ($config.sharpness -lt 0 -or $config.sharpness -gt 10) {
				$errosEncontrados += 7
			}
		} else {
			$errosEncontrados += 7
		}
	} else {
		$config.sharpness = 0 
	}

    # valida: CODEC
    if ($null -ne $config.codec) {
        $config.codec = [string]$config.codec.ToString().ToUpper()
        $codecsValidas = @("AVC", "HEVC")
        if ($config.codec -notin $codecsValidas) { $errosEncontrados += 14 }
    } else {
        $config.codec = "AVC"
    }

    # valida: HDR
    $valorFinalHdr = $false
    $existeHdr = $null -ne $config.hdr -and $config.hdr -ne ""
    if ($existeHdr) {
        $strTesteHdr = $config.hdr.ToString().ToLower().Trim()
        if ($strTesteHdr -notin @("true", "false")) {
            $errosEncontrados += 8
        } else {
            $valorFinalHdr = ($strTesteHdr -eq "true")
            
            # Se o HDR for ativado, o único codec aceito no momento é o HEVC
            if ($valorFinalHdr -and $config.codec -ne "HEVC") {
                $errosEncontrados += 15
            }
        }
    }
    $config.hdr = $valorFinalHdr

    # valida: SHUTDOWN
    $valorFinalShutdown = $false
    $existeShutdown = $null -ne $config.shutdown -and $config.shutdown -ne ""
    if ($existeShutdown) {
        $strTesteShutdown = $config.shutdown.ToString().ToLower().Trim()
        if ($strTesteShutdown -notin @("true", "false")) {
            $errosEncontrados += 9
        } else {
            $valorFinalShutdown = ($strTesteShutdown -eq "true")
        }
    }
    $config.shutdown = $valorFinalShutdown

    # valida: HUD_PORT
    if ($null -ne $config.hud_port -and $config.hud_port -as [int]) {
        $config.hud_port = [int]$config.hud_port
    } else {
        if ($null -eq $config.hud_port -or $config.hud_port -eq "") { 
            $config.hud_port = 4867 
        } else { 
            $errosEncontrados += 10 
        }
    }

    # valida: VERBOSE
    $valorFinalVerbose = $false
    $existeVerbose = $null -ne $config.verbose -and $config.verbose -ne ""
    if ($existeVerbose) {
        $strTesteVerbose = $config.verbose.ToString().ToLower().Trim()
        if ($strTesteVerbose -notin @("true", "false")) {
            $errosEncontrados += 11
        } else {
            $valorFinalVerbose = ($strTesteVerbose -eq "true")
        }
    }
    $config.verbose = $valorFinalVerbose

    # valida: GPU_ID
    if ($config.gpu_id -ne $null -and $config.gpu_id -match '^\d+$' -and [int]$config.gpu_id -ge 0) {
        $config.gpu_id = [int]$config.gpu_id
    } else {
        $errosEncontrados += 12
    }

    # valida: SIMULATE_GPU
	if ($null -ne $config.simulate_gpu -and $config.simulate_gpu -ne "") {
		$config.simulate_gpu = [string]$config.simulate_gpu.ToString().ToUpper().Trim()
		$gpusValidas = @("NONE", "NVIDIA", "AMD", "INTEL", "CPU")
		if ($config.simulate_gpu -notin $gpusValidas) { $errosEncontrados += 13 }
	} else {
		# Se veio nulo ou vazio (porque não foi digitado na CLI), assume o padrão maiúsculo para o seu if de hardware funcionar
		$config.simulate_gpu = "NONE"
	}
	
    # LOOP DE VERIFICAÇÃO DE ERROS
    if ($errosEncontrados.Count -gt 0) {
        foreach ($id in $errosEncontrados) {
            Write-Warning $catalogoErros[$id]
        }
        exit
    }
	
    return $config

}
