Write-Output "Compiling shaders..."

$types = @("*.vert", "*.frag", "*.tesc", "*.tese", "*.geom", "*.comp")
$files = Get-Childitem -Include $types -Recurse -File

foreach($file in $files)
{
    $command = "C:\VulkanSDK\1.1.85.0\Bin32\glslangValidator.exe -V $file -o $file.spv"
    #Write-Output $command
    Invoke-Expression $command
}

Write-Output "Done!"