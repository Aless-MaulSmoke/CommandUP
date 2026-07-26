# ==========================================================================
# ------------------------------
#
#   FSRIFS Core Processing Engine (v1.1.2)
#   Automated pipeline for video upscaling (FSR) and frame interpolation (IFS).
#
#   process.ps1 (Powershell script)
#   07/15/2026
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
#  -v
#   Switch flag to toggle detailed real-time rendering logs (Verbose mode).
#
#  -shutdown
#   Switch flag to trigger a safe 30-second system power-off countdown after execution.
#
#  -DRAG_CONFIG
#   Switch flag to automatically read and load parameters directly from DRAG_CONFIG.txt.
# ------------------------------

param(
    [string]$file,
    [string]$folder,
    [string]$scale,
    [int]$fps,
	[string]$interpolate = "none",
    [string]$quality = "MED",
	[System.Nullable[int]]$sharpness,
	[switch]$v,
	[switch]$shutdown,
	[switch]$DRAG_CONFIG
)

# ==========================================================================
# 1. FUNÇÃO DE CONFIGURAÇÃO E TRATAMENTO DE PARÂMETROS
# ==========================================================================
function Get-ScriptConfig {
    param (
        [System.Collections.IDictionary]$BoundParameters
    )

    $config = [PSCustomObject]@{
        file        = $null
        folder      = $null
        quality     = $null
        fps         = $null
        interpolate = $null
        scale       = $null
        sharpness   = $null
        verbose     = $null
        shutdown    = $null
        drag_config = $false
        v           = $null  # Alias para verbose
    }

    # Verifica se o parâmetro de comando -drag_config foi informado pelo usuário
    $usaDragConfig = $BoundParameters.ContainsKey('drag_config') -and $BoundParameters['drag_config'] -eq $true

    if ($usaDragConfig -and (Test-Path "$PSScriptRoot\DRAG_CONFIG.txt")) {
        # Usa parametros do arquivo de configuração
        if ($BoundParameters.ContainsKey('file'))   { $config.file   = $BoundParameters['file'] }
        if ($BoundParameters.ContainsKey('folder')) { $config.folder = $BoundParameters['folder'] }
        $config.drag_config = $true

        # Busca apenas as chaves necessárias no arquivo de texto
        $chavesNecessarias = @("QUALITY", "FPS", "INTERPOLATE", "SCALE", "SHARPNESS", "VERBOSE", "SHUTDOWN")
        $conteudoTxt = Get-Content "$PSScriptRoot\DRAG_CONFIG.txt"

        foreach ($chave in $chavesNecessarias) {
            $linha = $conteudoTxt | Where-Object { $_ -match "^$chave\s*=" }
            if ($linha) {
                $propriedade, $valor = $linha.Split('=', 2)
                $config.$($chave.ToLower()) = $valor.Trim()
            }
        }
    }
    else {
        # Usa apenas parametros informado pelo usuário
        foreach ($prop in $BoundParameters.GetEnumerator()) {
            $config.$($prop.Key.ToLower()) = $prop.Value
        }
    }

    # ==========================================================================
    # TRATAMENTO DE TIPOS, PADRONIZAÇÃO E VALIDAÇÃO FINAL
    # ==========================================================================
    
    # Catálogo de erros mapeado por IDs
    $catalogoErros = @{
        1 = "Ambiguity Error: Use ONLY '-file' OR '-folder', not both at the same time."
        2 = "No input specified. Use '-file' for a single video or '-folder' for batch processing."
        3 = "The 'quality' parameter only accepts one of these valid options: LOW | MED | BIG"
        4 = "The 'fps' parameter must be a valid number greater than 0."
        5 = "The 'interpolate' parameter only accepts one of these valid options: none | oversample | mitchell_clamp | linear"
        6 = "The 'scale' parameter should be a resolution, e.g., 1920x1080, or a scaling factor, e.g., 1.5"
        7 = "The 'sharpness' parameter only accepts numbers between 0 and 10"
        8 = "The 'verbose' parameter only accepts true or false."
        9 = "The 'shutdown' parameter only accepts true or false."
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
        if ($config.sharpness -as [int]) {
            $config.sharpness = [int]$config.sharpness
            if ($config.sharpness -lt 0 -or $config.sharpness -gt 10) {
                $errosEncontrados += 7
            }
        } else {
            $errosEncontrados += 7
        }
    } else {
        $config.sharpness = 0 
    }

    # valida: VERBOSE
    $valorFinalVerbose = $false
    $existeV = $null -ne $config.v
    $existeVerbose = $null -ne $config.verbose

    if ($existeV -and -not $existeVerbose) {
        $valorFinalVerbose = $true
    } else {
        $strTesteVerbose = if ($null -ne $config.verbose) { $config.verbose.ToString().ToLower().Trim() } else { "" }

        if ($existeVerbose -and $strTesteVerbose -notin @("true", "false") -and $strTesteVerbose -ne "") {
            $errosEncontrados += 8
        } else {
            $valorFinalVerbose = ($strTesteVerbose -eq "true")
        }
    }
    $config.verbose = $valorFinalVerbose

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

    # LOOP DE VERIFICAÇÃO DE ERROS
    if ($errosEncontrados.Count -gt 0) {
        foreach ($id in $errosEncontrados) {
            Write-Warning $catalogoErros[$id]
        }
        exit
    }

    return $config
}

# ==========================================================================
# 2. FUNÇÃO DE INICIALIZAÇÃO DO AMBIENTE E SHADER (GLOBAL)
# ==========================================================================
function Initialize-GlobalPipeline {
    param (
        [PSCustomObject]$Config
    )

    # Define os caminhos das ferramentas e estruturas do ambiente
    $pipeline = [PSCustomObject]@{
        ffmpeg       = "$PSScriptRoot\ffmpeg\bin\ffmpeg.exe"
        ffprobe      = "$PSScriptRoot\ffmpeg\bin\ffprobe.exe"
        logpath      = "$PSScriptRoot\log"
        shader       = "$PSScriptRoot\shaders\fsr.glsl"
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
		# Contingência universal via processador utilizando H.264 (Extremamente Leve)
		$Global:SelectedCodec = "libx264"
		$Global:CodecArgs = @("-crf", $pipeline.qp_i, "-preset", "ultrafast")
	}


    # Define argumentos verbose
    if ($Config.verbose) { 
		$pipeline.verboseArgs = @("-v", "verbose") 
	} else {
		$pipeline.verboseArgs = @("-v", "repeat+error", "-stats") 
	}

    # Aplicação GLOBAL da Nitidez (Sharpness) diretamente no arquivo de shader
    if ($Config.scale) {
        # Se omitido no CLI/TXT, assume o valor padrão 5 conforme o script original
        $sharpnessValor = if ($null -ne $Config.sharpness) { $Config.sharpness } else { 5 }
        $clampedUserSharpness = [math]::Max(0, [math]::Min(10, $sharpnessValor)) / 10.0
        $fsrSharpness = 2.0 * (1.0 - $clampedUserSharpness)

        if (Test-Path $pipeline.shader) {
            $linhasShader = Get-Content $pipeline.shader
            $novaLinhaSharpness = "#define SHARPNESS $($fsrSharpness.ToString('0.0', [System.Globalization.CultureInfo]::InvariantCulture))"
            
            for ($i = 0; $i -lt $linhasShader.Count; $i++) {
                if ($linhasShader[$i] -like "#define SHARPNESS*") {
                    $linhasShader[$i] = $novaLinhaSharpness
                    break
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

# ==========================================================================
# 3. FUNÇÃO DE CRIAÇÃO DO SPOOL DE PROCESSAMENTO (ARRAY)
# ==========================================================================
function Get-VideoQueue {
    param (
        [PSCustomObject]$Config
    )

    # Inicializa o array vazio que conterá todos os arquivos válidos
    $listaArquivos = @()
    
    # Definição das extensões de vídeo suportadas pelo seu pipeline original
    $extensoesSuportadas = @(".mp4", ".mkv")

    # CENÁRIO A: Processamento de arquivo único
    if ($Config.file) {
        # Resolve o caminho absoluto (caso o usuário tenha passado um caminho relativo)
        $caminhoAbsoluto = Resolve-Path -Path $Config.file -ErrorAction SilentlyContinue

        if (-not $caminhoAbsoluto -or -not (Test-Path $caminhoAbsoluto.Path)) {
            Write-Host "[CRITICAL ERROR] The specified file does not exist or the path is invalid:`n -> $($Config.file)" -ForegroundColor Red
            exit
        }

        # Extrai a extensão do arquivo para validação
        $extensao = [System.IO.Path]::GetExtension($caminhoAbsoluto.Path).ToLower()
        if ($extensao -notin $extensoesSuportadas) {
            Write-Host "[ERROR] Extension '$extensao' not supported. The script only accepts: $($extensoesSuportadas -join ', ')." -ForegroundColor Yellow
            exit
        }

        # Adiciona o arquivo único ao array de forma uniforme
        $listaArquivos += $caminhoAbsoluto.Path
    }
    
    # CENÁRIO B: Processamento em Lote
    elseif ($Config.folder) {
        # Resolve e valida o caminho absoluto da pasta
        $pastaAbsoluta = Resolve-Path -Path $Config.folder -ErrorAction SilentlyContinue

        if (-not $pastaAbsoluta -or -not (Test-Path $pastaAbsoluta.Path -PathType Container)) {
            Write-Host "[CRITICAL ERROR] The specified folder does not exist or is not a valid directory:`n -> $($Config.folder)" -ForegroundColor Red
            exit
        }

        Write-Host "Sweeping through the folder looking for valid videos..." -ForegroundColor Cyan
        
        # Busca recursiva por arquivos que possuam as extensões permitidas
        $arquivosEncontrados = Get-ChildItem -Path $pastaAbsoluta.Path -File -Recurse | 
                               Where-Object { $_.Extension.ToLower() -in $extensoesSuportadas }

        foreach ($arquivo in $arquivosEncontrados) {
            $listaArquivos += $arquivo.FullName
        }

        # Se a pasta estiver vazia ou sem vídeos compatíveis, aborta antes de iniciar o loop
        if ($listaArquivos.Count -eq 0) {
            Write-Host "[WARNING] No compatible video files ($($extensoesSuportadas -join ', ')) were found in the specified folder." -ForegroundColor Yellow
            exit
        }

        Write-Host "Processing queue created successfully! ($($listaArquivos.Count) file(s) found)." -ForegroundColor Green
    }

    # Retorna o array pronto seja com 1 elemento ou com vários
    return $listaArquivos
}

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
            Reason      = "Original resolution ($wOriginal`x$hOriginal) and FPS ($fpsOriginal) are already identical to the requested targets."
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

# ==========================================================================
# 5. FUNÇÃO DE EXECUÇÃO DE SPOOL DE VIDEOS DA PIPELINE
# ==========================================================================
function Invoke-VideoPipeline {
    param (
        [string]$VideoPath,
        [PSCustomObject]$Config,
        [PSCustomObject]$Pipeline,
        [PSCustomObject]$Metadata
    )

    # Gerenciamento de Logs (Subpasta apenas se for lote)
    $nomeSemExtensao = [System.IO.Path]::GetFileNameWithoutExtension($VideoPath)
    $diretorioLogAlvo = ""

    if ($Config.folder) {
        $nomePastaOrigem = [System.IO.Path]::GetFileName([System.IO.Path]::GetDirectoryName($VideoPath))
        if ([string]::IsNullOrEmpty($nomePastaOrigem)) { $nomePastaOrigem = "Lote_Processado" }
        
        $subpastaLog = Join-Path -Path $Pipeline.logpath -ChildPath $nomePastaOrigem
        if (-not (Test-Path $subpastaLog)) {
            New-Item -ItemType Directory -Path $subpastaLog -Force | Out-Null
        }
        $diretorioLogAlvo = $subpastaLog
    } 
    else {
        if (-not (Test-Path $Pipeline.logpath)) {
            New-Item -ItemType Directory -Path $Pipeline.logpath -Force | Out-Null
        }
        $diretorioLogAlvo = $Pipeline.logpath
    }
	
    # Resgate de Variáveis Locais
    $scale         = $Config.scale
    $fps           = $Config.fps
    $quality       = $Config.quality
    $sharpness     = $Config.sharpness
    $shaderFFmpeg  = $Pipeline.shaderFFmpeg
	$gpuColorFix   = $pipeline.gpuColorFix
    $wOriginal     = $Metadata.wOriginal
    $hOriginal     = $Metadata.hOriginal
    $widthOut      = $Metadata.widthOut
    $heightOut     = $Metadata.heightOut
    $inPix         = $Metadata.pixFormat
    $inSpace       = $Metadata.colorSpace
    $inRange       = $Metadata.colorRange
	$placeboRange  = if ($inRange -eq "limited" -or $inRange -eq "tv") { "tv" } else { "pc" }
	
    # Montagem Rígida dos Filtros e Sufixos Originais
	if ($gpuColorFix) { 
		$vfString  = "format=gbrp,"
		$formatFix = "format=gbrp,shuffleplanes=0:1:2:3,"
	} else {
		$vfString  = ""
		$formatFix = ""
	}
	$sufixo = "_QUALITY_$quality"
	
	# Validações finais
	if ($Metadata.skipIFS) { $pipeline.interpolate = "none" }
	
	# STATE 1: (ambos FSR e IFS ativos)
    if (-not $Metadata.skipFSR -and -not $Metadata.skipIFS) {
        $vfString += "hwupload,libplacebo=w=${widthOut}:h=${heightOut}:fps=${fps}:frame_mixer=$($pipeline.interpolate)"
        $sufixo += "_IFS_${fps}fps$($pipeline.interpolate.ToUpper())_FSR_${widthOut}x${heightOut}"
    }
    # STATE 2: (apenas FSR ativo)
    elseif (-not $Metadata.skipFSR -and $Metadata.skipIFS) {
        $vfString += "hwupload,libplacebo=w=${widthOut}:h=${heightOut}"
        $sufixo += "_FSR_${widthOut}x${heightOut}"
    }
    # STATE 3: (apenas IFS ativo))
    elseif ($Metadata.skipFSR -and -not $Metadata.skipIFS) {
        $vfString += "hwupload,libplacebo=w=${wOriginal}:h=${hOriginal}:fps=${fps}:frame_mixer=$($pipeline.interpolate)"
        $sufixo += "_IFS_${fps}fps$($pipeline.interpolate.ToUpper())"
    }
    # string final do parametro filters para libplacebo
	$vfString += ":colorspace=${inSpace}:color_primaries=${inSpace}:color_trc=${inSpace}:range=${inRange}:custom_shader_path='${shaderFFmpeg}',hwdownload,${formatFix}format=${inPix}"

    if ($null -ne $sharpness) { $sufixo += "_SHARPNESS_$sharpness" }

#debug    
#Write-Host "`n[ STRING DE VÍDEO ENVIADA AO FFMPEG ] >>> $vfString <<<`n" -ForegroundColor Yellow

    # Definição do Arquivo de Saída Sufixos no nome
    $pastaSaida = [System.IO.Path]::GetDirectoryName($VideoPath)
    $extensaoOriginal = [System.IO.Path]::GetExtension($VideoPath)
    $videoSaida = Join-Path -Path $pastaSaida -ChildPath "${nomeSemExtensao}${sufixo}${extensaoOriginal}"

    # Preparação das variáveis exatas da sua assinatura de comando
    $ffmpeg      = $Pipeline.ffmpeg
    $verboseArgs = if ($Pipeline.verboseArgs.Count -gt 0) { $Pipeline.verboseArgs } else { @() }
    $file        = $VideoPath
    $qp_i        = $Pipeline.qp_i
    $qp_p        = $Pipeline.qp_p
    $outFile     = $videoSaida
	
	# Inicia escrita do FFmpeg no arquivo de log 
	$logIndividual = Join-Path -Path $diretorioLogAlvo -ChildPath ([System.IO.Path]::ChangeExtension([System.IO.Path]::GetFileName($outFile), ".txt"))
	$ffmpegLogPath = $logIndividual.Replace('\', '/')
	$env:FFREPORT = "file='$ffmpegLogPath':level=32"

	$Resultado = [PSCustomObject]@{
		Success         = $false
		SkipVideo       = $false
		NomeArquivo     = $Metadata.NomeArquivo
		OutputFile      = $outFile
		LogPath         = $logIndividual
		TempoDecorrido  = [TimeSpan]::Zero
		DuracaoVideo    = $Metadata.duracaoSegundos
		widthOut        = $widthOut
		heightOut       = $heightOut
		fpsOut          = $Metadata.fpsOut
		Speed           = 0.0
		Bitrate         = "N/A"
		ErrorMessage    = $null
	}
			
    # Cronometragem do laço de processamento
    $cronometro = [System.Diagnostics.Stopwatch]::StartNew()
	
	# Exibe arquivo corrente
    $tsDuracao = [TimeSpan]::FromSeconds($Resultado.DuracaoVideo)
	$timeDuracao = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsDuracao.TotalHours), $tsDuracao.Minutes, $tsDuracao.Seconds
	Write-Host "`n[   File: $($Resultado.NomeArquivo)  Length: $($timeDuracao)  Resolution: $($wOriginal)x$($hOriginal)  FPS: $($metadata.fpsOriginal)   ]"  -ForegroundColor Gray
	
    try {
		
	    # Intervalo de cores não pode ser Completo
		# /**/ parece existir na libplacebo uma conversão ao de formato full para limited: realizar testes
		if ($placeboRange -eq "pc") {
			Throw "Full color format detected! Use Limited format."
		}
		
		# /**/
		# no modelo atual é necessario sempre enviar o device (por enquanto esta setado device 0 padrão, resolver como escolher vcard por id)
		#& $ffmpeg -init_hw_device vulkan=vk:0 -filter_hw_device vk -ignore_unknown $verboseArgs -i "$file" `
		& $ffmpeg -init_hw_device vulkan=vk:0 -filter_hw_device vk $verboseArgs -i "$file" `
			-vf "$vfString" `
			-fps_mode passthrough `
			-c:v $Global:SelectedCodec @Global:CodecArgs `
			-tag:v avc1 `
			-c:a copy -y `
			"$outFile"	
			
		$cronometro.Stop()
		$Resultado.TempoDecorrido = $cronometro.Elapsed

		if (Test-Path $outFile) {
			# Sucesso: Ativa a flag e calcula as métricas direto no objeto base
            $Resultado.Success = $true
            
            # Velocidade em linha única: Duração / Segundos Decorridos
            if ($cronometro.Elapsed.TotalSeconds -gt 0 -and $Metadata.duracaoSegundos -gt 0) {
                $Resultado.Speed = $Metadata.duracaoSegundos / $cronometro.Elapsed.TotalSeconds
            }

            # Busca o Bitrate de forma direta varrendo as últimas linhas do Log
            if (Test-Path $logIndividual) {
                $linhasLog = Get-Content $logIndividual -Tail 15 2>$null
                foreach ($linha in $linhasLog) {
                    if ($linha -match 'bitrate\s*=\s*([\d\.]+)\s*kb') {
                        $Resultado.Bitrate = $Matches[1] 
                        break
                    }
                }
            }
        } else {
            throw "The final file wasn't generated on disk."
        }
    } catch {
        if ($cronometro.IsRunning) { $cronometro.Stop() }
        $Resultado.Success        = $false
        $Resultado.TempoDecorrido = $cronometro.Elapsed
		$Resultado.ErrorMessage   = $_.Exception.Message + ". Error during the native FFmpeg call."
	} finally {
        # Limpa o escopo do ambiente para blindar o próximo arquivo do loop
        Remove-Item Env:\FFREPORT -ErrorAction SilentlyContinue
    }
	
    return $Resultado
	
}

# ==========================================================================
# 6. FUNÇÃO DE PROCESSAMENTO DE STATUS E MÉTRICAS
# ==========================================================================
function Show-VideoStatus {
    param (
        [PSCustomObject]$Result,
        [ref]$StatusAcumulado
    )

    $nomeArquivo = $Result.NomeArquivo

    # Se o arquivo foi pulado por redundância
    if ($Result.SkipVideo) {
        Write-Host "`n[SKIPPED] $nomeArquivo" -ForegroundColor Yellow
        Write-Host "`          $($Result.Reason)" -ForegroundColor Yellow
        $StatusAcumulado.Value.TotalPulados++
        return
    }

    # Se o arquivo falhou no FFmpeg ou FFprobe
    if (-not $Result.Success) {
        Write-Host "`n[FAIL]    $nomeArquivo" -ForegroundColor Red
        Write-Host "          $($Result.ErrorMessage)" -ForegroundColor Red
        Write-Host "          Check the log file." -ForegroundColor DarkGray
        $StatusAcumulado.Value.TotalFalhas++
        return
    }
	
    # Inicializa variáveis para extração do Log original
    $tamanhoFinalStr = "N/A"
    $bitrateStr = "N/A"
    $tamanhoBytes = 0

    # Extração de Métricas do Log (Lógica original do seu script)
    if (Test-Path $Result.LogPath) {
        $conteudoLog = Get-Content $Result.LogPath -Tail 30 2>$null
        foreach ($linha in $conteudoLog) {
            # Regex robusta para capturar o bitrate do sumário consolidado (ex: bitrate=17791.5kbits/s)
            if ($linha -match 'bitrate\s*=\s*([\d\.]+)\s*kb') {
                $bitrateStr = "$($Matches[1]) kb/s"
            }
            # Se encontrar o sumário final de frames processados, confirma o tamanho físico em disco
            if ($linha -match 'Lsize=' -and (Test-Path $Result.OutputFile)) {
                $item = Get-Item $Result.OutputFile
                $tamanhoBytes = $item.Length
                $tamanhoFinalStr = "$([math]::Round($tamanhoBytes / 1MB, 2)) MB"
            }
        }
    }

    # Caso a leitura do log falhe em pegar o tamanho, tenta ler direto do disco como contingência
    if ($tamanhoFinalStr -eq "N/A" -and (Test-Path $Result.OutputFile)) {
        $tamanhoBytes = (Get-Item $Result.OutputFile).Length
        $tamanhoFinalStr = "$([math]::Round($tamanhoBytes / 1MB, 2)) MB"
    }

    # Formata o tempo de renderização (mm:ss)
    $tempoRender = "{0:d2}:{1:d2}" -f $Result.TempoDecorrido.Minutes, $Result.TempoDecorrido.Seconds

    # Cálculo da velocidade comparada ao tempo real do vídeo (Speed Factor)
    $speedFactor = "N/A"
    if ($Result.DuracaoVideo -gt 0 -and $Result.TempoDecorrido.TotalSeconds -gt 0) {
        $speed = [math]::Round($Result.DuracaoVideo / $Result.TempoDecorrido.TotalSeconds, 2)
        $speedFactor = "$($speed.ToString('0.0', [System.Globalization.CultureInfo]::InvariantCulture))x"
    }

    # EXIBIÇÃO EM LINHA ÚNICA (Exatamente como planejado)
    Write-Host "[SUCCESS] " -NoNewline -ForegroundColor Green
    Write-Host "$nomeArquivo " -NoNewline -ForegroundColor White
    Write-Host "| Time: $tempoRender | Speed: $speedFactor | Size: $tamanhoFinalStr | Bitrate: $bitrateStr" -ForegroundColor Gray

    # Acumula os dados globais para o relatório final
	$StatusAcumulado.Value.TotalSucesso++
	$StatusAcumulado.Value.TempoTotalSegundos += $Result.TempoDecorrido.TotalSeconds
	$StatusAcumulado.Value.TamanhoTotalBytes += $tamanhoBytes
	$StatusAcumulado.Value.DuracaoTotalVideos += $Result.DuracaoVideo

}

# ==========================================================================
# 7. RELATÓRIO FINAL E AUTOMAÇÃO DE DESLIGAMENTO
# ==========================================================================
function Out-GlobalSummary {
    param (
        [PSCustomObject]$StatusAcumulado,
        [boolean]$ShutdownAtivo,
		[PSCustomObject]$Pipeline
    )
	
	# Converte os tempos acumulados para um formato legível (hh:mm:ss)
    $tsRender = [TimeSpan]::FromSeconds($StatusAcumulado.TempoTotalSegundos)
	$tempoRenderTotal = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsRender.TotalHours), $tsRender.Minutes, $tsRender.Seconds

    $tsVideos = [TimeSpan]::FromSeconds($StatusAcumulado.DuracaoTotalVideos)
    $tempoVideoTotal = "{0:d2}:{1:d2}:{2:d2}" -f [int]$tsVideos.TotalHours, $tsVideos.Minutes, $tsVideos.Seconds

    $tamanhoTotalMB = [math]::Round($StatusAcumulado.TamanhoTotalBytes / 1MB, 2)
	
	# Renderiza o Banner de Estatísticas Consolidadas
    # Tenta obter dados do último processo global para o cabeçalho descritivo
	$InfoBanner = if ($Global:LastProcessResult) { $Global:LastProcessResult } else { $Global:SessionHistory[0] }

    # MODO INDIVIDUAL: Caso apenas 1 vídeo tenha rodado na fila
	if ($StatusAcumulado.TotalSucesso -eq 1 -and $Global:LastProcessResult -and -not $Config.folder) {
		$Result = $Global:LastProcessResult
		$velocidadeStr = if ($Result.Speed) { "$([math]::Round($Result.Speed, 2))x" } else { "1.00x" }
		$bitrateStr    = if ($Result.Bitrate) { "$($Result.Bitrate) kbits/s" } else { "N/A" }

		Write-Host "`n"
		Write-Host "====================================================================================" -ForegroundColor Cyan
		Write-Host "  Total Render Time : " -NoNewline; Write-Host "$tempoRenderTotal" -ForegroundColor Yellow
		Write-Host "  Processing Speed  : " -NoNewline; Write-Host "$velocidadeStr" -ForegroundColor Yellow
		Write-Host "  Final File Size   : " -NoNewline; Write-Host "$tamanhoTotalMB MB" -ForegroundColor Yellow
		Write-Host "  Video Bitrate     : " -NoNewline; Write-Host "$bitrateStr" -ForegroundColor Yellow
		Write-Host "  Session Log Saved : " -NoNewline; Write-Host "OK" -ForegroundColor Yellow

	} else {
        # MODO LOTE: Cabeçalhos com as novas colunas perfeitamente espaçadas
		Write-Host "`n"
		Write-Host "====================================================================================" -ForegroundColor Cyan
        Write-Host " STATUS    | SPEED      | TIME     | FILE " -ForegroundColor Yellow
        Write-Host "------------------------------------------------------------------------------------" -ForegroundColor DarkGray

		if ($Global:SessionHistory) {
			foreach ($Item in $Global:SessionHistory) {
				# Trunca e formata o nome do arquivo dando o espaço ideal restante (45 caracteres)
				$nomeCurto = if ($Item.NomeArquivo.Length -gt 45) { $Item.NomeArquivo.Substring(0, 42) + "..." } else { $Item.NomeArquivo.PadRight(45) }
				
				if ($Item.Success -and -not $Item.SkipVideo) {
					$velStr   = if ($Item.Speed) { "$([math]::Round($Item.Speed, 1))x".PadRight(10) } else { "1.0x      " }
					# Formata o cronômetro individual do vídeo para hh:mm:ss
					$tempoStr = [string]::Format("{0:d2}:{1:d2}:{2:d2}", $Item.TempoDecorrido.Hours, $Item.TempoDecorrido.Minutes, $Item.TempoDecorrido.Seconds).PadRight(8)

					Write-Host " [SUCCESS]" -ForegroundColor Green -NoNewline; Write-Host " | $velStr | $tempoStr | $nomeCurto" -ForegroundColor White
				} elseif ($Item.SkipVideo) {
					# Preenchimento visual limpo para itens pulados/redundantes
					Write-Host " [SKIPPED]" -ForegroundColor Yellow -NoNewline; Write-Host " | ----       | --:--:-- | $nomeCurto" -ForegroundColor White
				} else {
					Write-Host " [FAILED] " -ForegroundColor Red -NoNewline; Write-Host " | ----       | --:--:-- | $nomeCurto" -ForegroundColor White
				}
			}
		} else {
            Write-Host " No batch history found in the spool." -ForegroundColor Gray
        }

		Write-Host "------------------------------------------------------------------------------------" -ForegroundColor DarkGray
		Write-Host "  Videos Processed Successfully  : " -NoNewline; Write-Host "$($StatusAcumulado.TotalSucesso)" -ForegroundColor Green
		Write-Host "  Ignored Videos (Redundant)     : " -NoNewline; Write-Host "$($StatusAcumulado.TotalPulados)" -ForegroundColor Yellow
		Write-Host "  Videos with Process Error      : " -NoNewline; Write-Host "$($StatusAcumulado.TotalFalhas)" -ForegroundColor Red
		Write-Host "------------------------------------------------------------------------------------" -ForegroundColor DarkGray
		Write-Host "  Total Video Duration Processed : $tempoVideoTotal" -ForegroundColor White
		Write-Host "  Total Rendering Time           : $tempoRenderTotal" -ForegroundColor White
		Write-Host "  Total Disk Space Used          : $tamanhoTotalMB MB" -ForegroundColor White
		
	}

	Write-Host "====================================================================================" -ForegroundColor Cyan
	Write-Host "  vCard (GPU): $($Pipeline.gpuName) [Codec: $Global:SelectedCodec] " -ForegroundColor Cyan
	Write-Host "------------------------------------------------------------------------------------" -ForegroundColor DarkGray
	Write-Host "                    __          _  __               _   _  ___                      " -ForegroundColor Red
	Write-Host "                   / _|___ _ __(_)/ _|___    __   _/ | / ||_  |                     " -ForegroundColor Red
	Write-Host "                  | |_/ __| '__| | |_/ __|   \ \ / / | | | / /                      " -ForegroundColor Red
	Write-Host "                  |  _\__ \ |  | |  _\__ \    \ V /| |_| |/ /_                      " -ForegroundColor Red
	Write-Host "                  |_| |___/_|  |_|_| |___/     \_/ |_(_)_(_)__|                     " -ForegroundColor White
	Write-Host "                                                                                    " -ForegroundColor White
	Write-Host "            Author: Aless (MaulSmoke) | Community: YouTube (@toplayaless)           " -ForegroundColor Gray
	Write-Host "                                                                                    " -ForegroundColor Gray
	Write-Host "------------------------------------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host "  Quality: $($Config.quality.ToUpper())  Resolution: $($InfoBanner.widthOut)x$($InfoBanner.heightOut)  Sharpness: $($Config.sharpness)  FPS: $($InfoBanner.fpsOut)  Interp: $($Config.interpolate)" -ForegroundColor White
	Write-Host "====================================================================================" -ForegroundColor Cyan

    # Lógica de Desligamento Automático
	if ($ShutdownAtivo -and $StatusAcumulado.TotalSucesso -gt 0) {
		Write-Host "`nThe -shutdown parameter is active. The system will shut down." -ForegroundColor Yellow
		Write-Host "Press [ESC] to CANCEL or [ENTER] to SHUT DOWN IMMEDIATELY." -ForegroundColor White

		$segundosRestantes = 30
		while ($segundosRestantes -gt 0) {
			Write-Host "`rShutting down in $segundosRestantes seconds...  " -NoNewline -ForegroundColor Red
			
			# contagem
			for ($i = 0; $i -lt 10; $i++) {
				Start-Sleep -Milliseconds 100
				
				# usando .NET para funcionar mesmo que a janela do terminal perca o foco
				if ([Console]::KeyAvailable) {
					$tecla = [Console]::ReadKey($true)
					if ($tecla.Key -eq "Escape") {
						Write-Host "`n[CANCELLED] Automatic shutdown interrupted by the user." -ForegroundColor Green
						return
					}
					if ($tecla.Key -eq "Enter") {
						$segundosRestantes = 0
						break
					}
				}
			}
			$segundosRestantes--
		}

		Write-Host "`nStarting system shutdown..." -ForegroundColor Red
		Stop-Computer -Force
	}
}

# ==========================================================================
# BLOCO PRINCIPAL DE EXECUÇÃO
# ==========================================================================

# 1. Captura e unifica as configurações gerais
$Config = Get-ScriptConfig -BoundParameters $PSBoundParameters

# 2. Inicializa o ambiente global e shaders
$Pipeline = Initialize-GlobalPipeline -Config $Config

# 3. Cria a fila uniforme de processamento
$FilaTrabalho = Get-VideoQueue -Config $Config

# 4. Inicializa o objeto acumulador para as Estatísticas Gerais
$StatusGeral = [PSCustomObject]@{
    TotalSucesso         = 0
    TotalPulados         = 0
    TotalFalhas          = 0
    TempoTotalSegundos   = 0.0
    TamanhoTotalBytes    = [long]0
    DuracaoTotalVideos   = 0.0
}

Write-Host "`nStarting to process the queue..." -ForegroundColor Cyan
Write-Host "----------------------------------------------------------------------" -ForegroundColor DarkGray

# 5. O Loop de Processamento processando cada arquivo de forma isolada
foreach ($VideoAtual in $FilaTrabalho) {
    
    # Passo A: Extração de Metadados e Validação de Redundância por Arquivo
    $Metadata = Get-VideoMetadataAndValidate -VideoPath $VideoAtual -Config $Config -Pipeline $Pipeline
    
    # Se o arquivo foi pulado por redundância ou falha no FFprobe
	if ($Metadata.SkipVideo) {
		$Global:SessionHistory += $Metadata
		Show-VideoStatus -Result $Metadata -StatusAcumulado ([ref]$StatusGeral)
		continue 
	}

    # Passo B: Execução da Pipeline do FFmpeg para o vídeo atual
	$Global:LastProcessResult = Invoke-VideoPipeline -VideoPath $VideoAtual -Config $Config -Pipeline $Pipeline -Metadata $Metadata
	$ResultadoProcesso = $Global:LastProcessResult
	$Global:SessionHistory += $Global:LastProcessResult
    
    # Passo C: Exibição de Conclusão em Linha Única e Acumulação de Métricas
    Show-VideoStatus -Result $ResultadoProcesso -StatusAcumulado ([ref]$StatusGeral)
}

# 6. Exibição da Estatística Geral de todos os arquivos e Gatilho de Desligamento
Out-GlobalSummary -StatusAcumulado $StatusGeral -ShutdownAtivo $Config.shutdown -Pipeline $Pipeline


