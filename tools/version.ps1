#!/usr/bin/env powershell

param (
  [Parameter(Position = 0, Mandatory = $false)]
  [string]$BuildRoot = $null,
  [Parameter(Position = 1, Mandatory = $false)]
  [string]$SourceRoot = $null
)

$lastSvnRevision = 6962
$lastSvnHash = '16cd907fe7482cb54a7374cd28b8501f138116be'
$defineNumberMatch = [regex] '^#define\s+(\w+)\s+(\d+)$'
$defineStringMatch = [regex] "^#define\s+(\w+)\s+[`"']?(.+?)[`"']?$"
$semVerMatch = [regex] '^v?(\d+)\.(\d+)\.(\d+)(?:-(\w+))?$'
$sanaeBetaMatch = [regex] '^sanae-beta-(\d+)$'

$repositoryRootPath = Join-Path $PSScriptRoot .. | Resolve-Path

if ($BuildRoot -eq $null -or $BuildRoot.Trim() -eq "")  {
  $BuildRoot = $repositoryRootPath
}

# support legacy in-tree builds
if ([System.IO.Path]::GetFullPath([System.IO.Path]::Combine((pwd).Path, $BuildRoot)) -eq
  [System.IO.Path]::GetFullPath([System.IO.Path]::Combine((pwd).Path, $repositoryRootPath))) {
    $BuildRoot = Join-Path $repositoryRootPath 'build'
  }
$gitVersionHeaderPath = Join-Path $BuildRoot 'git_version.h'
$sourceVersionHeaderPath = Join-Path $repositoryRootPath 'git_version.h'

git -C $repositoryRootPath rev-parse --is-inside-work-tree 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
  if ((Test-Path $sourceVersionHeaderPath) -and ($sourceVersionHeaderPath -ne $gitVersionHeaderPath)) {
    Copy-Item $sourceVersionHeaderPath $gitVersionHeaderPath -Force
  }
  if (Test-Path $gitVersionHeaderPath) { exit 0 }
  throw "$repositoryRootPath is not a git repository and has no generated git_version.h"
}

$version = @{}
if (Test-Path $gitVersionHeaderPath) {
  Get-Content $gitVersionHeaderPath | %{$_.Trim()} | ?{$_} | %{
    switch -regex ($_) {
      $defineNumberMatch {
        $version[$Matches[1]] = [int]$Matches[2];
      }
      $defineStringMatch {
        $version[$Matches[1]] = $Matches[2];
      }
    }
  }
}
$gitRevision = $lastSvnRevision + ((git -C $repositoryRootPath log --pretty=oneline "$($lastSvnHash)..HEAD" 2>$null | Measure-Object).Count)
$gitBranch = git -C $repositoryRootPath symbolic-ref --short HEAD 2>$null
$gitHash = git -C $repositoryRootPath rev-parse --short HEAD 2>$null
$gitVersionString = $gitRevision, $gitBranch, $gitHash -join '-'
$exactGitTag = git -C $repositoryRootPath describe --exact-match --tags 2>$null

$version['TAGGED_RELEASE'] = $false
$version['RESOURCE_BASE_VERSION'] = @(3, 5, 4)
$version['INSTALLER_VERSION'] = '3.5.4'
# Keep this script ASCII-only: Windows PowerShell 5.1 reads UTF-8 without a
# BOM as an ANSI code page and can misparse a literal em dash as a quote.
$emDash = [char]0x2014
$version['SANAE_VERSION_STRING'] = 'sanae-v0.4 (beta)'
$version['SANAE_PRODUCT_STRING'] = "Sanae $emDash Aegisub v0.4 (beta)"
$version['SANAE_RELEASE_TAG'] = 'sanae-beta-04'
$version['SANAE_BETA_NUMBER'] = 4

if ($exactGitTag -match $sanaeBetaMatch) {
  $betaNumber = [int]$Matches[1]
  $betaText = $betaNumber.ToString('00')
  $version['TAGGED_RELEASE'] = $true
  $version['RESOURCE_BASE_VERSION'] = @(3, 5, $betaNumber)
  $version['INSTALLER_VERSION'] = "3.5.$betaNumber"
  $version['SANAE_VERSION_STRING'] = "sanae-v0.$betaNumber (beta)"
  $version['SANAE_PRODUCT_STRING'] = "Sanae $emDash Aegisub v0.$betaNumber (beta)"
  $version['SANAE_RELEASE_TAG'] = "sanae-beta-$betaText"
  $version['SANAE_BETA_NUMBER'] = $betaNumber
  $gitVersionString = "Sanae-beta-$betaText"
} elseif ($exactGitTag -match $semVerMatch) {
  $version['TAGGED_RELEASE'] = $true
  $version['RESOURCE_BASE_VERSION'] = $Matches[1..3]
  $joinedVersion = $Matches[1..3] -join '.'

  $gitVersionString = $joinedVersion + @("-$($Matches[4])",'')[!$Matches[4]]
  $version['INSTALLER_VERSION'] = $joinedVersion
} elseif ($exactGitTag) {
  # Keep development defaults for unrelated tags.
} else {
  foreach ($rev in (git -C $repositoryRootPath rev-list --tags 2>$null)) {
    $tag = git -C $repositoryRootPath describe --exact-match --tags $rev 2>$null
    if ($tag -match $semVerMatch) {#
      $version['TAGGED_RELEASE'] = $false
      $version['RESOURCE_BASE_VERSION'] = $Matches[1..3]
      $version['INSTALLER_VERSION'] = ($Matches[1..3] -join '.')
      break;
    }
  }
}

$version['BUILD_GIT_VERSION_NUMBER'] = $gitRevision
$version['BUILD_GIT_VERSION_STRING'] = $gitVersionString
$version['BUILD_GIT_HASH'] = $gitHash

$headerLines = $version.GetEnumerator() | Sort-Object Key | %{
  $type = $_.Value.GetType()
  $value = $_.Value
  $fmtValue = switch ($type) {
    ([string]) {"`"$value`""}
    ([int]) {$value.ToString()}
    ([bool]) {([int]$value).ToString()}
    ([object[]]) {$value -join ', '}
    default {
      Write-Host "no format known for type '$type' - trying default string conversion" -ForegroundColor Red
      {"`"$($value.ToString())`""}
    }
  }
  "#define $($_.Key) $($fmtValue)"
}
$headerText = ($headerLines -join "`r`n") + "`r`n"

if ((Test-Path $gitVersionHeaderPath) -and
    ([System.IO.File]::ReadAllText($gitVersionHeaderPath) -eq $headerText)) {
  exit 0
}

# UTF-8 without a BOM is accepted by MSVC's /utf-8 mode and avoids changing
# the encoding assumptions of generated C/C++ headers.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($gitVersionHeaderPath, $headerText, $utf8NoBom)
