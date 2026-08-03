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
	$hdr           = $Config.hdr
	$hud_port      = $Config.hud_port
	$gpu_id        = $Config.gpu_id
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
	
	$vfString += "hwupload,libplacebo=w=${widthOut}:h=${heightOut}"
	$sufixo = "_QUALITY_$quality"
	
	# FSR ativo
    if (-not $Metadata.skipFSR) {
        $sufixo += "_FSR_${widthOut}x${heightOut}"
		if ($null -ne $sharpness) { $sufixo += "_SHARPNESS_$sharpness" }
		if ($hdr -eq $true -or $hdr -eq "true") { $sufixo += "_HDR" }
    }
    # IFS ativo
    if (-not $Metadata.skipIFS) {
        $vfString += ":fps=${fps}:frame_mixer=$($pipeline.interpolate)"
        $sufixo += "_IFS_${fps}fps$($pipeline.interpolate.ToUpper())"
    }
    # string final do parametro filters para libplacebo
	$vfString += ":colorspace=${inSpace}:color_primaries=${inSpace}:color_trc=${inSpace}:range=${inRange}:custom_shader_path='${shaderFFmpeg}',hwdownload,${formatFix}format=${inPix}"

#debug    
#Write-Host "`n[ vfString ] $vfString `n" -ForegroundColor Yellow

    # Definição do Arquivo de Saída Sufixos no nome
    $pastaSaida = [System.IO.Path]::GetDirectoryName($VideoPath)
    $extensaoOriginal = [System.IO.Path]::GetExtension($VideoPath)
    $videoSaida = "${nomeSemExtensao}${sufixo}${extensaoOriginal}"

    # Preparação das variáveis exatas da assinatura de comando
    $ffmpeg      = $Pipeline.ffmpeg
    $verboseArgs = if ($Pipeline.verboseArgs.Count -gt 0) { $Pipeline.verboseArgs } else { @() }
    $file        = $VideoPath
    $qp_i        = $Pipeline.qp_i
    $qp_p        = $Pipeline.qp_p
    $outFile     = Join-Path -Path $pastaSaida -ChildPath $videoSaida
	
	# Inicia escrita do FFmpeg no arquivo de log 
	$logIndividual = Join-Path -Path $diretorioLogAlvo -ChildPath ([System.IO.Path]::ChangeExtension([System.IO.Path]::GetFileName($outFile), ".txt"))
	$ffmpegLogPath = $logIndividual.Replace('\', '/')
	$env:FFREPORT = "file='$ffmpegLogPath':level=32"

	$Resultado = [PSCustomObject]@{
		Success         = $false
		SkipVideo       = $false
		NomeArquivo     = $Metadata.NomeArquivo
		PastaSaida      = $pastaSaida
		VideoSaida      = $videoSaida
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
    $tsDuracao = [TimeSpan]::FromSeconds($Resultado.DuracaoVideo)
	$timeDuracao = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsDuracao.TotalHours), $tsDuracao.Minutes, $tsDuracao.Seconds
			
    # Cronometragem do laço de processamento
    $cronometro = [System.Diagnostics.Stopwatch]::StartNew()
	
	
    try {
		
	    # Intervalo de cores não pode ser Completo
		# /**/ parece existir na libplacebo uma conversão do formato full para limited: realizar testes
		if ($placeboRange -eq "pc") {
			Throw "Full color format detected! Use Limited format."
		}
		
		# Inicializa a escuta TCP
		$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $hud_port)
		$listener.Start()

		[System.Threading.Thread]::Sleep(50)

		# Prepara os argumentos adicionais com base no VERBOSE do CONFIG
		$vArgsTextoLimpo = ""
		if ($verboseArgs) { 
			$vArgsTextoLimpo = ($verboseArgs -join ' ') -replace "-stats", ""
			if ($vArgsTextoLimpo) { $vArgsTextoLimpo += " " }
		}

		# Monta a string do FFmpeg
		$argumentosString = '-nostats -progress "tcp://127.0.0.1:' + $hud_port + '" ' +
							'-init_hw_device vulkan=vk:' + $gpu_id + ' -filter_hw_device vk ' +
							$vArgsTextoLimpo +
							'-i "' + $file + '" ' +
							'-vf "' + $vfString + '" ' +
							'-fps_mode passthrough ' +
							'-c:v ' + $Global:SelectedCodec + ' ' +
							($Global:CodecArgs -join ' ') + ' ' +
							'-tag:v avc1 -c:a copy -y -gpu ' + $gpu_id + ' "' + $outFile + '"'

#debug    
#Write-Host "`n[ argumentosString ] $argumentosString `n" -ForegroundColor Yellow

		# Dispara o FFmpeg enviando o fluxo de texto gerado para o arquivo de log
		$processo = Start-Process -FilePath $ffmpeg -ArgumentList $argumentosString `
								  -NoNewWindow -PassThru `
								  -RedirectStandardError $logIndividual

		# Aguarda a sincronização do Socket
		$timeoutContador = 0
		while (-not $listener.Pending() -and $timeoutContador -lt 50 -and -not $processo.HasExited) {
			Start-Sleep -Milliseconds 100
			$timeoutContador++
		}

		$clienteSocket = $null
		$leitor = $null

		try {
			if ($listener.Pending()) {
				$clienteSocket = $listener.AcceptTcpClient()
				$stream = $clienteSocket.GetStream()
				$leitor = [System.IO.StreamReader]::new($stream)
				
				$outTimeMs = 0
				$velocidade = 1.0
				
				# Oculta o cursor do terminal
				[Console]::CursorVisible = $false

				# ------------------------------------------------------------------
				# Layout estático (antes do loop iniciar)
				# ------------------------------------------------------------------
				$layoutEstatico = @"

------------------------------------------------------------------------------------
  [File     ]: $($Metadata.NomeArquivo)
  [Format   ]: $($Metadata.wOriginal)x$($Metadata.hOriginal)/$($metadata.fpsOriginal) -> $($Metadata.widthOut)x$($Metadata.heightOut)/$($Metadata.fpsOut)
  [Length   ]: $($timeDuracao)
"@
				Write-Host $layoutEstatico
				
				$ponteiros = @("-", "\", "|", "/")
				$ponteiroPos = 0
				
				# Captura a posição do cursor após os dados estáticos. 
				$posicaoOriginalCursor = $Host.UI.RawUI.CursorPosition
				
				# LOOP DE PROCESSAMENTO DO HUD
				while (-not $processo.HasExited -or $stream.DataAvailable) {
					if ($stream.DataAvailable) {
						$linha = $leitor.ReadLine()

						if ($null -eq $linha) { break }
						
						if ($linha -match "out_time_ms=(\d+)") { 
							$outTimeMs = [double]$Matches[1] 
						}
						elseif ($linha -match "speed=\s*([\d\.]+)x") { 
							$velocidade = [double]$Matches[1] 
						}
						elseif ($linha -match "fps=\s*([\d\.]+)") { 
							$velocidadeFps = [double]$Matches[1] 
						}
						elseif ($linha -match "progress=(.*)") {
							
							# Lógica de cálculo matemático do progresso
							$tempoTotalMs = $Metadata.duracaoSegundos * 1000000
							$porcentagem = 0
							if ($tempoTotalMs -gt 0) {
								$porcentagem = [int][math]::Min(100, [math]::Round(($outTimeMs / $tempoTotalMs) * 100, 0))
							}                    
							$tempoAtualStr = "00:00:00"
							if ($outTimeMs -gt 0) {
								# Converte microssegundos do FFmpeg para segundos e depois para TimeSpan
								$tsAtual = [TimeSpan]::FromSeconds($outTimeMs / 1000000)
								$tempoAtualStr = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsAtual.TotalHours), $tsAtual.Minutes, $tsAtual.Seconds
							}
							$restanteStr = "00:00:00"
							if ($velocidade -gt 0 -and $outTimeMs -lt $tempoTotalMs) {
								$milisegundosRestantes = ($tempoTotalMs - $outTimeMs) / $velocidade
								if ($milisegundosRestantes -gt 0) {
									$tsEta = [TimeSpan]::FromMilliseconds($milisegundosRestantes / 1000)
									$restanteStr = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsEta.TotalHours), $tsEta.Minutes, $tsEta.Seconds
								}
							}
							$tsDecorrido = $cronometro.Elapsed
							$decorridoStr = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsDecorrido.TotalHours), $tsDecorrido.Minutes, $tsDecorrido.Seconds

							
							# Montagem da Barra Visual (25 blocos)
							$larguraBarra = 25
							$preenchido = [int][math]::Round(($porcentagem / 100) * $larguraBarra)
							$vazio = $larguraBarra - $preenchido
							$barraVisual = ("■" * $preenchido) + ("-" * $vazio)
							
							# LAYOUT DINÂMICO (Apenas o que muda)
							$layoutDinamico = @"
  [Done     ]: $($tempoAtualStr)
  [Progress ]: [$barraVisual] $porcentagem% 
  [Speed    ]: $($velocidade.ToString('0.00'))x  $($ponteiros[$ponteiroPos])  $($velocidadeFps)fps     
  [Elapsed  ]: $decorridoStr 
  [Left     ]: $restanteStr   
"@
							# Incrementa a animação do ponteiro
							$ponteiroPos = ($ponteiroPos + 1) % $ponteiros.Count
							
							# Reposiciona o cursor no ponto fixo e atualiza apenas as 3 linhas finais
							$Host.UI.RawUI.CursorPosition = $posicaoOriginalCursor
							Write-Host $layoutDinamico -NoNewline
						}
					}
					# Parece ser redundante essa espera, pois o if anterior ja controla o fluxo
					#Start-Sleep -Milliseconds 5
				}
				
				# ------------------------------------------------------------------
				# Finaliza com atualização manual
				# ------------------------------------------------------------------
				$barraVisualFinal = "■" * 25
				$layoutDinamicoFinal = @"
  [Done     ]: $($timeDuracao)
  [Progress ]: [$barraVisualFinal] 100%
  [Speed    ]: $($velocidade.ToString('0.00'))x 
  [Elapsed  ]: $decorridoStr 
  [Left     ]: $restanteStr
"@
				$Host.UI.RawUI.CursorPosition = $posicaoOriginalCursor
				Write-Host $layoutDinamicoFinal -NoNewline
				
				# Exibe novamente o cursor do terminal
				[Console]::CursorVisible = $true
				
			} else {
				Write-Warning "Impossible to connect to the HUD socket: $($hud_port)."
				exit
			}
		}
		catch {
			Throw "Impossible to connect to the HUD socket: $($hud_port)."
		}
		finally {
			if ($null -ne $leitor) { $leitor.Close() }
			if ($null -ne $clienteSocket) { $clienteSocket.Close() }
			$listener.Stop()
		}

		# Salta linhas para descolar o relatório de sucesso do fechamento do HUD
		Write-Host ""
		Write-Host ""

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
