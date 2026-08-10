[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string] $AppName,

    [string] $ProjectPath = $PSScriptRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertTo-IdentifierWords {
    param([string] $Value)

    $withWordBoundaries = $Value -creplace '([a-z0-9])([A-Z])', '$1 $2'
    $words = @($withWordBoundaries -split '[^A-Za-z0-9]+' | Where-Object { $_ })
    if ($words.Count -eq 0) {
        throw "AppName must contain at least one letter or number."
    }

    return @($words | ForEach-Object { $_.ToLowerInvariant() })
}

$resolvedProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$words = ConvertTo-IdentifierWords -Value $AppName
$pascalName = ($words | ForEach-Object {
        $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1)
    }) -join ''
$snakeName = $words -join '_'
$kebabName = $words -join '-'
$displayName = ($words | ForEach-Object {
        $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1)
    }) -join ' '

if ($pascalName -notmatch '^[A-Za-z][A-Za-z0-9]*$') {
    throw "AppName produces an invalid C++/CMake identifier: '$pascalName'."
}

$replacements = [ordered]@{
    'FltkSystemTrayAppTemplate' = $pascalName
    'SystemTrayAppTemplate' = $pascalName
    'fltk-system-tray-app-template' = $kebabName
    'system_tray_app_core' = "${snakeName}_core"
    'System Tray App Template' = $displayName
    'System Tray App' = $displayName
}

$textExtensions = @('.cmake', '.cpp', '.h', '.hpp', '.json', '.md', '.ps1', '.rc', '.txt')
$files = Get-ChildItem -LiteralPath $resolvedProjectPath -Recurse -File | Where-Object {
    $relativePath = [IO.Path]::GetRelativePath($resolvedProjectPath, $_.FullName)
    $_.FullName -notmatch '[\\/]build[\\/]' -and
        $_.FullName -notmatch '[\\/]\.git[\\/]' -and
        $textExtensions -contains $_.Extension.ToLowerInvariant() -and
        $relativePath -ne 'Rename-Template.ps1'
}

foreach ($file in $files) {
    $content = [IO.File]::ReadAllText($file.FullName)
    $updatedContent = $content
    foreach ($replacement in $replacements.GetEnumerator()) {
        $updatedContent = $updatedContent.Replace($replacement.Key, $replacement.Value)
    }

    if ($updatedContent -eq $content) {
        continue
    }

    if ($PSCmdlet.ShouldProcess($file.FullName, "replace template names")) {
        [IO.File]::WriteAllText($file.FullName, $updatedContent, [Text.UTF8Encoding]::new($false))
        Write-Output "Updated $([IO.Path]::GetRelativePath($resolvedProjectPath, $file.FullName))"
    }
}

Write-Output "App name: $displayName"
Write-Output "Identifier: $pascalName"
Write-Output "Project package: $kebabName"
