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
		format       = "mp4"
		quality      = "MED"
		fps          = $null
		interpolate  = "none"
		scale        = $null
		sharpness    = 5
		codec        = "AVC"
		hdr          = $false
		shutdown     = $false
		drag_config  = $false
		port         = 50548
		verbose      = $false
		gpu_id       = 0
		simulate     = "NONE"
		debug        = $false
    }

	# Descobre a pasta raiz subindo um nível a partir da pasta 'functions'
	$caminhoDragConfig = Join-Path $Global:CUP_ROOT "DRAG_CONFIG.txt"
	$caminhoConfigIni  = Join-Path $Global:CUP_ROOT "CONFIG.ini"

	# Carrega parametros do config.ini
	if (Test-Path $caminhoConfigIni) {
		# Busca apenas as chaves necessárias no arquivo de texto
		$chavesNecessarias = @("PORT", "VERBOSE", "GPU_ID", "SIMULATE", "DEBUG")
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
			$chavesNecessarias = @("FORMAT", "QUALITY", "FPS", "INTERPOLATE", "SCALE", "SHARPNESS", "CODEC", "HDR", "SHUTDOWN")
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
		10 = "The 'port' the parameter must be a valid integer number."
		11 = "The 'verbose' parameter only accepts true or false."
		12 = "The 'gpu_id' parameter must be a valid number."
		13 = "The 'simulate' parameter only accepts one of these valid options: none | nvidia | amd | intel | cpu "
		14 = "The 'codec' parameter only accepts one of these valid options: avc | hevc "
		15 = "The 'hdr' parameter requires the codec to be hevc."
		16 = "The 'debug' parameter only accepts true or false."
		17 = "The 'format' parameter only accepts one of these valid formats: mp4 | mkv "

    }
    $errosEncontrados = @()

    # valida: FILE e FOLDER ----------------------------------------------------
    if ($config.file -and $config.folder) { $errosEncontrados += 1 }
    if (-not $config.file -and -not $config.folder) { $errosEncontrados += 2 }

    # valida: FORMAT -----------------------------------------------------------
	$config.format = $config.format.ToLower().Trim()
	if (-not (Match_Validation $config.format @("mp4", "mkv"))) {
		$errosEncontrados += 16
	}

    # valida: QUALITY ----------------------------------------------------------
	$config.quality = $config.quality.ToUpper().Trim()
	if (-not (Match_Validation $config.quality @("LOW", "MED", "BIG"))) {
		$errosEncontrados += 3
	}

    # valida: FPS --------------------------------------------------------------
    if ($null -ne $config.fps -and $config.fps -as [int]) {
        $config.fps = [int]$config.fps
        if ($config.fps -le 0) { $errosEncontrados += 4 }
    } else {
        if ($null -eq $config.fps) { $config.fps = 0 } else { $errosEncontrados += 4 }
    }

    # valida: INTERPOLATE ------------------------------------------------------
	$config.interpolate = $config.interpolate.ToLower().Trim()
	if (-not (Match_Validation $config.interpolate @("none", "oversample", "mitchell_clamp", "linear"))) {
		$errosEncontrados += 5
	}

    # valida: SCALE ------------------------------------------------------------
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

	# valida: SHARPNESS --------------------------------------------------------
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

    # valida: SHUTDOWN ---------------------------------------------------------
	if (-not (Boolean_Validation $config.shutdown)) {
		$errosEncontrados += 9
	}
	$config.shutdown = [bool]::Parse($config.shutdown)

    # valida: PORT
    if ($null -ne $config.port -and $config.port -as [int]) {
        $config.port = [int]$config.port
    } else {
        if ($null -eq $config.port -or $config.port -eq "") { 
            $config.port = 50548 
        } else { 
            $errosEncontrados += 10 
        }
    }

    # valida: VERBOSE ----------------------------------------------------------
	if (-not (Boolean_Validation $config.verbose)) {
		$errosEncontrados += 11
	}
	$config.verbose = [bool]::Parse($config.verbose)
	
    # valida: GPU_ID -----------------------------------------------------------
    if ($config.gpu_id -ne $null -and $config.gpu_id -match '^\d+$' -and [int]$config.gpu_id -ge 0) {
        $config.gpu_id = [int]$config.gpu_id
    } else {
        $errosEncontrados += 12
    }

    # valida: SIMULATE ---------------------------------------------------------
	$config.simulate = $config.simulate.ToUpper().Trim()
	if (-not (Match_Validation $config.simulate @("NONE", "NVIDIA", "AMD", "INTEL", "CPU"))) {
		$errosEncontrados += 13
	}

    # valida: CODEC ------------------------------------------------------------
	$config.codec = $config.codec.ToUpper().Trim()
	if (-not (Match_Validation $config.codec @("AVC", "HEVC"))) {
		$errosEncontrados += 14
	}

    # valida: HDR --------------------------------------------------------------
	if (-not (Boolean_Validation $config.hdr)) {
		$errosEncontrados += 8
	} else {
		# Se o HDR for ativado, o único codec aceito no momento é o HEVC
		$config.hdr = [bool]::Parse($config.hdr)
		if ($config.hdr -and $config.codec -ne "HEVC") {
			$errosEncontrados += 15
		}
	}

    # valida: DEBUG ------------------------------------------------------------
	if (-not (Boolean_Validation $config.debug)) {
		$errosEncontrados += 16
	}
	$config.debug = [bool]::Parse($config.debug)


    # LOOP DE VERIFICAÇÃO DE ERROS
    if ($errosEncontrados.Count -gt 0) {
        foreach ($id in $errosEncontrados) {
            Write-Warning $catalogoErros[$id]
        }
        exit
    }
	
	#debug
	if ($config.debug -eq $true) {
		Write-Host "`n[ parameters ] $($config) `n" -ForegroundColor Yellow
	}

    return $config

}

# Valida se é booleano
function Boolean_Validation($value) {
	$str = $value.ToString().ToLower().Trim()
	if (-not (Match_Validation $str @("true", "false"))) {
		return $false
	}

	return $true
	
}

# Valida se existe na lista
function Match_Validation($value, [array]$list) {
    return $Value -in $list
}

