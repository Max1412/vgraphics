Write-Output "Compiling shaders..."

$types = @("*.vert", "*.frag", "*.tesc", "*.tese", "*.geom", "*.comp", "*.rgen", "*.rchit", "*.rmiss")
$files = Get-Childitem -Include $types -Recurse -File

foreach($file in $files)
{
    #$command = "C:\VulkanSDK\1.1.92.0\Bin\glslc.exe $file -o $file.spv -c"
    #$command = "D:\mmader\vgraphics\shaders\glslangvalidator.exe -V $file -o $file.spv"
    $command = "C:\VulkanSDK\1.1.92.0\Bin\glslangvalidator.exe -V $file -o $file.spv"

    #Write-Output $command
    Invoke-Expression $command
}

Write-Output "Done!"