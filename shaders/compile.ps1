Write-Output "Compiling shaders..."

$types = @("*.vert", "*.frag", "*.tesc", "*.tese", "*.geom", "*.comp", "*.rgen", "*.rchit", "*.rmiss")
$files = Get-Childitem -Include $types -Recurse -File


foreach($file in $files)
{
    #$command = "$Env:VK_SDK_PATH\Bin\glslc.exe $file -o $file.spv -c"
    $command = "$Env:VK_SDK_PATH\Bin\glslangvalidator.exe -V $file -o $file.spv"

    #Write-Output $command
    Invoke-Expression $command
}

Write-Output "Done!"