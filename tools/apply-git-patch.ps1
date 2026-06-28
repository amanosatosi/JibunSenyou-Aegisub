function Apply-GitPatch {
	param(
		[Parameter(Mandatory = $true)][string]$RepoDir,
		[Parameter(Mandatory = $true)][string]$PatchPath
	)

	$resolvedPatch = (Resolve-Path -LiteralPath $PatchPath).ProviderPath
	Push-Location $RepoDir
	try {
		git apply --check --ignore-whitespace --whitespace=nowarn $resolvedPatch 2>$null
		if ($LASTEXITCODE -eq 0) {
			Write-Host "Applying patch $resolvedPatch"
			git apply --ignore-whitespace --whitespace=nowarn $resolvedPatch
			if(!$?) { Exit $LASTEXITCODE }
			return
		}

		git apply --reverse --check --ignore-whitespace --whitespace=nowarn $resolvedPatch 2>$null
		if ($LASTEXITCODE -eq 0) {
			Write-Host "Patch already applied: $resolvedPatch"
			return
		}

		Write-Error "Patch cannot be applied cleanly: $resolvedPatch"
		git apply --check --ignore-whitespace --whitespace=nowarn $resolvedPatch
		if(!$?) { Exit $LASTEXITCODE }
	}
	finally {
		Pop-Location
	}
}
