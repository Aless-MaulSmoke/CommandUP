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
		Write-Host "  Output File       : " -NoNewline; Write-Host "$($Result.VideoSaida)" -ForegroundColor Yellow
		Write-Host "  Total Render Time : " -NoNewline; Write-Host "$tempoRenderTotal" -ForegroundColor Yellow
		Write-Host "  Processing Speed  : " -NoNewline; Write-Host "$velocidadeStr" -ForegroundColor Yellow
		Write-Host "  Final File Size   : " -NoNewline; Write-Host "$tamanhoTotalMB MB" -ForegroundColor Yellow
		Write-Host "  Video Bitrate     : " -NoNewline; Write-Host "$bitrateStr" -ForegroundColor Yellow
		Write-Host "  Session Log Saved : " -NoNewline; Write-Host "OK" -ForegroundColor Yellow

	} else {
        # MODO LOTE: Cabeçalhos com titulos das colunas
		Write-Host "`n"
		Write-Host "====================================================================================" -ForegroundColor Cyan
		Write-Host " STATUS    | SPEED      | ELAPSED  | FILE " -ForegroundColor Yellow
		Write-Host "------------------------------------------------------------------------------------" -ForegroundColor DarkGray

		if ($Global:SessionHistory) {
			foreach ($Item in $Global:SessionHistory) {
				if ($Item.Success -and -not $Item.SkipVideo) {
					$velStr   = if ($Item.Speed) { "$([math]::Round($Item.Speed, 1))x".PadRight(10) } else { "1.0x      " }
					# Formata o cronômetro individual do vídeo para hh:mm:ss
					$tempoStr = [string]::Format("{0:d2}:{1:d2}:{2:d2}", $Item.TempoDecorrido.Hours, $Item.TempoDecorrido.Minutes, $Item.TempoDecorrido.Seconds).PadRight(8)

					Write-Host " [SUCCESS]" -ForegroundColor Green -NoNewline; Write-Host " | $velStr | $tempoStr | $($Item.VideoSaida)" -ForegroundColor White
				} elseif ($Item.SkipVideo) {
					Write-Host " [SKIPPED]" -ForegroundColor Yellow -NoNewline; Write-Host " | ----       | --:--:-- | $($Item.NomeArquivo)" -ForegroundColor White
				} else {
					Write-Host " [FAILED] " -ForegroundColor Red -NoNewline; Write-Host " | ----       | --:--:-- | $($Item.NomeArquivo)" -ForegroundColor White
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
	Write-Host "    _________                                           .___  ____ _____________    " -ForegroundColor Red
	Write-Host "    \_   ___ \  ____   _____   _____ _____    ____    __| _/ |    |   \______   \   " -ForegroundColor Red
	Write-Host "    /    \  \/ /  _ \ /     \ /     \\__  \  /    \  / __ |  |    |   /|     ___/   " -ForegroundColor Red
	Write-Host "    \     \___(  <_> )  Y Y  \  Y Y  \/ __ \|   |  \/ /_/ |  |    |  / |    |       " -ForegroundColor Red
 	Write-Host "     \______  /\____/|__|_|  /__|_|  (____  /___|  /\____ |  |______/  |____|       " -ForegroundColor White
	Write-Host "            \/             \/      \/     \/     \/      \/                         " -ForegroundColor White
	Write-Host "                                                                                    "
	Write-Host "        [ Author  : Aless(MaulSmoke) | Community: YT/toPlayAless ]     cup v1.0.2   " -ForegroundColor Gray
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
				
				# desligando via .NET para evitar bug de não contar/desligar quando a janela não tem o foco
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
