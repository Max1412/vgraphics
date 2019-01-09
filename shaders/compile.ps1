Write-Output "Compiling shaders..."

$types = @("*.vert", "*.frag", "*.tesc", "*.tese", "*.geom", "*.comp", "*.rgen", "*.rchit", "*.rmiss", "*.rahit")
$files = Get-Childitem -Include $types -Recurse -File


foreach($file in $files)
{
    $command = "$Env:VK_SDK_PATH\Bin\glslc.exe $file -o $file.spv -c -I include"
    #$command = "$Env:VK_SDK_PATH\Bin\glslangvalidator.exe -V $file -o $file.spv"

    #Write-Output $command
        $name = [System.IO.Path]::GetFileName($file)
        $end = [System.IO.Path]::GetDirectoryName($file).split("\")
        $lastDirName = $end[$end.Count - 1]
    Write-Output $lastDirName\$name
    Invoke-Expression $command
}

Write-Output "Done!"