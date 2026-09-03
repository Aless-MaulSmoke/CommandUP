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
	$format        = $Config.format
    $scale         = $Config.scale
    $fps           = $Config.fps
    $quality       = $Config.quality
    $sharpness     = $Config.sharpness
	$codec         = $Config.codec
	$hdr           = $Config.hdr
	$port          = $Config.port
	$gpu_id        = $Config.gpu_id
    $shaderFFmpeg  = $Pipeline.shaderFFmpeg
	$gpuName       = $Pipeline.gpuName
	$gpuColorFix   = $pipeline.gpuColorFix
	$gpuVulkanArgs = $pipeline.gpuVulkanArgs
    $wOriginal     = $Metadata.wOriginal
    $hOriginal     = $Metadata.hOriginal
    $widthOut      = $Metadata.widthOut
    $heightOut     = $Metadata.heightOut
    $inPix         = $Metadata.pixFormat
    $inRange       = $Metadata.colorRange
    $inSpace       = $Metadata.colorSpace
	$inPrimaries   = $Metadata.colorPrimaries
	$inTrc         = $Metadata.colorTransfer
	$bitsFormat    = $Metadata.bitsFormat
	$bitsOutput    = $Metadata.bitsOutput
	$bitsDowngrade = $Metadata.bitsDowngrade
	$placeboRange  = if ($inRange -eq "limited" -or $inRange -eq "tv") { "tv" } else { "pc" }
	
	# Conversão de formatos para range Full
	$mapaFormatosFull = @{
		"yuvj420p"   = "yuv420p"
		"yuvj422p"   = "yuv422p"
		"yuvj444p"   = "yuv444p"
		"nv12"       = "nv12"
		"p010le"     = "p010le"
		"p010"       = "p010le"
		"yuv420p10le"= "yuv420p10le"
		"yuv444p10le"= "yuv444p10le"
	}
	
	# Conversão de 10 para 8bits usando formatos planares
	$mapaFormatosDown = @{
		"p010"        = "nv12"
		"p010le"      = "nv12"
		"yuv420p10le" = "yuv420p"
		"yuv444p10le" = "yuv444p"
	}
	
	$vfString  = ""
	$formatFix = ""

	# Trata formatos se colorRange for Full
	if ($placeboRange -eq "pc" -and -not $gpuColorFix) {
		if ($mapaFormatosFull.ContainsKey($inPix)) {
			$inPix = $mapaFormatosFull[$inPix]
		}
	}

	# Trata o downgrade de 10 para 8 bits
	if (($bitsFormat -eq 10 -and $codec -eq "avc") -or $Metadata.bitsDowngrade -eq $true) {
		if ($mapaFormatosDown.ContainsKey($inPix)) {
			$inPix = $mapaFormatosDown[$inPix]
		}
	}
	
	# Trata colorFix
	if ($gpuColorFix) {
		if ($placeboRange -eq "pc") {
			$vfString = "scale=in_range=pc:out_range=pc,format=gbrp,"
		} else {
			$vfString = "format=gbrp,"
		}
		$formatFix = "format=gbrp,shuffleplanes=0:1:2:3,"
	} elseif ($placeboRange -eq "pc") {
		$vfString  = "scale=in_range=pc:out_range=pc,format=${inPix},"
	}
	
	$vfString += "hwupload,libplacebo=w=${widthOut}:h=${heightOut}"
	$sufixo = "_QUALITY_$quality"
	
	# FSR ativo
    if (-not $Metadata.skipFSR) {
        $sufixo += "_FSR_${widthOut}x${heightOut}"
		if ($null -ne $sharpness) { $sufixo += "_SHARPNESS_$sharpness" }
		if ($hdr -eq $true) { $sufixo += "_HDR" }
    }
    # IFS ativo
    if (-not $Metadata.skipIFS) {
        $vfString += ":fps=${fps}:frame_mixer=$($Config.interpolate)"
        $sufixo += "_IFS_${fps}fps$($Config.interpolate.ToUpper())"
    }

    # string final do parametro filters para libplacebo
	$vfString += ":colorspace=${inSpace}:color_primaries=${inPrimaries}:color_trc=${inTrc}:range=${inRange}:custom_shader_path='${shaderFFmpeg}',hwdownload,${formatFix}format=${inPix}"

    # Definição do Arquivo de Saída usando parametro format
    $pastaSaida = [System.IO.Path]::GetDirectoryName($VideoPath)
    $extensaoOriginal = [System.IO.Path]::GetExtension($VideoPath)
	$extensaoParametro = ".$format"
    $videoSaida = "${nomeSemExtensao}${sufixo}${extensaoParametro}"

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
		DuracaoVideo    = $Metadata.duracaoSecs
		widthOut        = $widthOut
		heightOut       = $heightOut
		fpsOut          = $Metadata.fpsOut
		Speed           = 0.0
		Bitrate         = "N/A"
		bitsDowngrade   = $bitsDowngrade
		ErrorMessage    = $null
	}
    $tsDuracao = [TimeSpan]::FromSeconds($Resultado.DuracaoVideo)
	$timeDuracao = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsDuracao.TotalHours), $tsDuracao.Minutes, $tsDuracao.Seconds
			
    # Cronometragem do laço de processamento
    $cronometro = [System.Diagnostics.Stopwatch]::StartNew()
	
	
    try {
		
		# Inicializa a escuta TCP
		$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $port)
		$listener.Start()

		[System.Threading.Thread]::Sleep(50)

		# Prepara os argumentos adicionais com base no VERBOSE do CONFIG
		$vArgsTextoLimpo = ""
		if ($verboseArgs) { 
			$vArgsTextoLimpo = ($verboseArgs -join ' ') -replace "-stats", ""
			if ($vArgsTextoLimpo) { $vArgsTextoLimpo += " " }
		}
		
		# Ajusta audio e legendas dependendo do formato
		$formatosArgs = @{"mp4" = "-c:a copy -y "; "mkv" = "-c:a copy -c:s copy -y " }

		# Seta a tag correta: avc1 para AVC (H.264) ou hvc1 para HEVC (H.265)
		$codecArgs = @{"avc" = "avc1 "; "hevc" = "hvc1 " }

		# Monta a string do FFmpeg
		$argumentosString = '-nostats -progress "tcp://127.0.0.1:' + $port + '" ' +
							'-init_hw_device vulkan=vk:' + $gpu_id + $gpuVulkanArgs + ' -filter_hw_device vk ' +
							$vArgsTextoLimpo +
							'-i "' + $file + '" ' +
							'-vf "' + $vfString + '" ' +
							'-fps_mode passthrough ' +
							'-c:v ' + $Global:SelectedCodec + ' ' +
							($Global:CodecArgs -join ' ') + ' ' +
							'-tag:v ' + $codecArgs[$codec] + $formatosArgs[$format] + '"' + $outFile + '"'

		#debug
		if ($Config.debug -eq $true) {
			Write-Host "`n[ argumentosString ] $argumentosString `n" -ForegroundColor Yellow
		}

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

--------------------------------------------------------[ Press Q key to abort ]----
  [File     ]: $($Metadata.NomeArquivo)
  [Format   ]: $($Metadata.wOriginal)x$($Metadata.hOriginal)/$($metadata.fpsOriginal) $($metadata.bitsFormat)-Bit -> $($Metadata.widthOut)x$($Metadata.heightOut)/$($Metadata.fpsOut) $($metadata.bitsOutput)-Bit 
  [Length   ]: $($timeDuracao)
"@
				Write-Host $layoutEstatico
				
				$animacao = @("-", "\", "|", "/")
				$animacaoPos = 0
				$porcentagem = 0
				$larguraBarra = 25
				$tempoAtualStr = "00:00:00"
				$restanteStr = "00:00:00"

				Write-Host "  Loading...  "  -ForegroundColor Yellow
				Write-Host "`n`n`n`n"
				
				# Captura a posição do cursor após os dados estáticos. 
				$posicaoOriginalCursor = $Host.UI.RawUI.CursorPosition
				$posicaoOriginalCursor.Y = $posicaoOriginalCursor.Y - 6
				$posicaoOriginalCursor.X = 0

				# LOOP DE PROCESSAMENTO DO HUD
				while (-not $processo.HasExited -or $stream.DataAvailable) {

					# verifica cancelamento pelo usuario
					if ([Console]::KeyAvailable) {
						$tecla = [Console]::ReadKey($true)
						if ($tecla.Key -eq 'Q') {
							# Finaliza o FFmpeg imediatamente
							$processo | Stop-Process -Force -ErrorAction SilentlyContinue

							#  Fecha cirurgicamente os sockets para liberar a porta $port
							if ($null -ne $leitor) { $leitor.Close(); $leitor.Dispose() }
							if ($null -ne $clienteSocket) { $clienteSocket.Close(); $clienteSocket.Dispose() }
							if ($null -ne $listener) { $listener.Stop() }

							# Alimenta o objeto de resultado com a falha controlada
							$Resultado.Success = $false
							$Resultado.ErrorMessage = "Process canceled by the user pressing [ Q ]"
							
							# Restaura o cursor e sai da função de forma limpa
							[Console]::CursorVisible = $true
							return $Resultado
						}
						
					}
		
					if ($stream.DataAvailable) {
						$linha = $leitor.ReadLine()

						if ($null -eq $linha) { 
							if ($processo.HasExited -and $processo.ExitCode -ne 0) {
								throw "Critical error, pipeline finished. (ExitCode: $($processo.ExitCode))."
							}
							break 
						}
						
						if ($linha -match "out_time_ms=(\d+)") { 
							$outTimeMs = [double]$Matches[1] 
						} elseif ($linha -match "speed=\s*([\d\.]+)x") { 
							$velocidade = [double]$Matches[1] 
						} elseif ($linha -match "fps=\s*([\d\.]+)") { 
							$velocidadeFps = [double]$Matches[1] 
						} elseif ($linha -match "progress=(.*)") {
							$statusProgresso = $Matches[1].Trim()

							# Calcula a matemática apenas se o processo estiver rodando
							if ($statusProgresso -eq "continue") {
								
								$tempoTotalMs = $Metadata.duracaoSecs * 1000000
								if ($tempoTotalMs -gt 0) {
									$porcentagem = [int][math]::Min(100, [math]::Round(($outTimeMs / $tempoTotalMs) * 100, 0))
								}                    
								if ($outTimeMs -gt 0) {
									$tsAtual = [TimeSpan]::FromSeconds($outTimeMs / 1000000)
									$tempoAtualStr = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsAtual.TotalHours), $tsAtual.Minutes, $tsAtual.Seconds
								}
								if ($velocidade -gt 0 -and $outTimeMs -lt $tempoTotalMs) {
									$milisegundosRestantes = ($tempoTotalMs - $outTimeMs) / $velocidade
									if ($milisegundosRestantes -gt 0) {
										$tsEta = [TimeSpan]::FromMilliseconds($milisegundosRestantes / 1000)
										$restanteStr = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsEta.TotalHours), $tsEta.Minutes, $tsEta.Seconds
									}
								}
								
							} elseif ($statusProgresso -eq "end") {
								# Se for o fim, crava os valores de sucesso e para o cálculo
								$porcentagem = 100
								$restanteStr = "00:00:00"
							}

							$tsDecorrido = $cronometro.Elapsed
							$decorridoStr = "{0:d2}:{1:d2}:{2:d2}" -f [int][math]::Truncate($tsDecorrido.TotalHours), $tsDecorrido.Minutes, $tsDecorrido.Seconds

							# Montagem da Barra Visual
							$preenchido = [int][math]::Round(($porcentagem / 100) * $larguraBarra)
							$vazio = $larguraBarra - $preenchido
							$barraVisual = ("■" * $preenchido) + ("-" * $vazio)
							
							# LAYOUT DINÂMICO (Apenas o que muda)
							$layoutDinamico = @"
  [Done     ]: $($tempoAtualStr)
  [Progress ]: [$barraVisual] $porcentagem% 
  [Speed    ]: $($velocidade.ToString('0.00'))x  $($animacao[$animacaoPos])  $($velocidadeFps)fps     
  [Elapsed  ]: $decorridoStr 
  [Left     ]: $restanteStr   
"@
							# Incrementa a animação do ponteiro
							$animacaoPos = ($animacaoPos + 1) % $animacao.Count
							
							# Reposiciona o cursor no ponto fixo e atualiza apenas as 3 linhas finais
							$Host.UI.RawUI.CursorPosition = $posicaoOriginalCursor
							Write-Host $layoutDinamico -NoNewline
						}
					}
				}
				
				# Exibe novamente o cursor do terminal
				[Console]::CursorVisible = $true
				
				if ($porcentagem -ne 0) {
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

				} else {
					Throw "Critical error in video encoding!"
				}

			} else {
				# Se o timeout estourou ou o processo morreu antes de conectar
				Throw  "Impossible to connect to the HUD socket: $($port)."
			}
		} catch {
			Throw $_.Exception.Message 
			
		} finally {
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
            if ($cronometro.Elapsed.TotalSeconds -gt 0 -and $Metadata.duracaoSecs -gt 0) {
                $Resultado.Speed = $Metadata.duracaoSecs / $cronometro.Elapsed.TotalSeconds
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
		$Resultado.ErrorMessage   = $_.Exception.Message
	} finally {
        # Limpa o escopo do ambiente para blindar o próximo arquivo do loop
        Remove-Item Env:\FFREPORT -ErrorAction SilentlyContinue
    }
	
    return $Resultado
	
}
