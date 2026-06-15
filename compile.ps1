param(
    [switch]$MergeStreams
)
Write-Host "###########################################################################" -ForegroundColor DarkBlue
$gcc_args = @('main.c', '-o', 'main.exe', '-O0', '-g', '-Wall', '-std=c99', '-Irl/include/', '-Lrl/lib/', '-lraylib', '-lopengl32', '-lgdi32', '-lwinmm')
if ($MergeStreams) {
    & gcc $gcc_args 2>&1
} else {
    & gcc $gcc_args
}
if ($LASTEXITCODE -eq 0) {
    & gdb .\main.exe --eval-command run
}

