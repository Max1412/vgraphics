param(
    [string]$Folder = "",
    [string]$Compiler = "glslc",
    [string]$Flags = ""
)
$startoutput = "Compiling shaders using " + $Compiler
Write-Output $startoutput

$types = @("*.vert", "*.frag", "*.tesc", "*.tese", "*.geom", "*.comp", "*.rgen", "*.rchit", "*.rmiss", "*.rahit")
$files = Get-Childitem $Folder -Include $types -Recurse -File


foreach($file in $files)
{
    if($Compiler -eq "glslc")
    {
        $command = "$Env:VK_SDK_PATH\Bin\glslc.exe $file -o $file.spv -c -I include $Flags --target-env=vulkan1.1"

        $name = [System.IO.Path]::GetFileName($file)
        $end = [System.IO.Path]::GetDirectoryName($file).split("\")
        $lastDirName = $end[$end.Count - 1]
        Write-Output $lastDirName\$name
        Invoke-Expression $command
    }
    elseif($Compiler -eq "glslangvalidator")
    {
        $command = "$Env:VK_SDK_PATH\Bin\glslangvalidator.exe -V $file -o $file.spv"
        Invoke-Expression $command
    }
    else
    {
        Write-Output "Please choose glslc or glslangvalidator as compiler"
        break
    }
}

$date = Get-Date -Format g
$finishOutput = "Done: " + $date
Write-Output $finishOutput 