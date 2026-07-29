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
