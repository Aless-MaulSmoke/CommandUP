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
		Write-Host "`n------------------------------------------------------------------------------------" -ForegroundColor White
		Write-Host "[SKIPPED] $nomeArquivo" -ForegroundColor Yellow
		Write-Host "`          $($Result.Reason)" -ForegroundColor Yellow
		$StatusAcumulado.Value.TotalPulados++
		return
    }

    # Se o arquivo falhou no FFmpeg ou FFprobe
    if (-not $Result.Success) {
		Write-Host "`n------------------------------------------------------------------------------------" -ForegroundColor White
		Write-Host "[FAIL]    $nomeArquivo" -ForegroundColor Red
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

    # Mini relatório em linha única
    Write-Host "[SUCCESS] " -NoNewline -ForegroundColor Green
    Write-Host "| Time: $tempoRender | Speed: $speedFactor | Size: $tamanhoFinalStr | Bitrate: $bitrateStr" -ForegroundColor Gray

    # Acumula os dados globais para o relatório final
	$StatusAcumulado.Value.TotalSucesso++
	$StatusAcumulado.Value.TempoTotalSegundos += $Result.TempoDecorrido.TotalSeconds
	$StatusAcumulado.Value.TamanhoTotalBytes += $tamanhoBytes
	$StatusAcumulado.Value.DuracaoTotalVideos += $Result.DuracaoVideo

}
