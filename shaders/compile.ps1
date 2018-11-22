Write-Output "Compiling shaders..."

$types = @("*.vert", "*.frag", "*.tesc", "*.tese", "*.geom", "*.comp", "*.rgen")
$files = Get-Childitem -Include $types -Recurse -File

foreach($file in $files)
{
    $command = "D:\mmader\shaderc\BUILD\glslc\Debug\glslc.exe $file -o $file.spv -c"
    #Write-Output $command
    Invoke-Expression $command
}

Write-Output "Done!"