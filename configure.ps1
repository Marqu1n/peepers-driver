# Peepers Driver Configuration Script
# PowerShell version with advanced features
# Run as Administrator

param(
    [string]$ApiUrl,
    [string]$ServerHost,
    [int]$ServerPort,
    [switch]$SetUser,
    [switch]$SetSystem,
    [switch]$SetRegistry,
    [switch]$View,
    [switch]$Remove,
    [switch]$Help
)

# Check if running as administrator
function Test-Administrator {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Display help
function Show-Help {
    Write-Host "Peepers Driver Configuration Script" -ForegroundColor Green
    Write-Host "====================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Usage:" -ForegroundColor Yellow
    Write-Host "  .\configure.ps1 [parameters]" -ForegroundColor White
    Write-Host ""
    Write-Host "Parameters:" -ForegroundColor Yellow
    Write-Host "  -ApiUrl <url>       Remote API URL" -ForegroundColor White
    Write-Host "  -ServerHost <host>  Webhook server host (default: 0.0.0.0)" -ForegroundColor White
    Write-Host "  -ServerPort <port>  Webhook server port (default: 8888)" -ForegroundColor White
    Write-Host ""
    Write-Host "Actions:" -ForegroundColor Yellow
    Write-Host "  -SetUser           Set user environment variables" -ForegroundColor White
    Write-Host "  -SetSystem         Set system environment variables" -ForegroundColor White
    Write-Host "  -SetRegistry       Set registry values" -ForegroundColor White
    Write-Host "  -View              View current configuration" -ForegroundColor White
    Write-Host "  -Remove            Remove all configuration" -ForegroundColor White
    Write-Host "  -Help              Show this help" -ForegroundColor White
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Yellow
    Write-Host "  .\configure.ps1 -ApiUrl 'http://api.example.com/webhook' -SetRegistry" -ForegroundColor White
    Write-Host "  .\configure.ps1 -View" -ForegroundColor White
    Write-Host "  .\configure.ps1 -Remove" -ForegroundColor White
}

# Set user environment variables
function Set-UserEnvironment {
    param($ApiUrl, $ServerHost, $ServerPort)
    
    Write-Host "Setting user environment variables..." -ForegroundColor Green
    
    if ($ApiUrl) {
        [Environment]::SetEnvironmentVariable("PEEPERS_API_URL", $ApiUrl, "User")
        Write-Host "  PEEPERS_API_URL = $ApiUrl" -ForegroundColor White
    }
    
    if ($ServerHost -and $ServerHost -ne "0.0.0.0") {
        [Environment]::SetEnvironmentVariable("PEEPERS_SERVER_HOST", $ServerHost, "User")
        Write-Host "  PEEPERS_SERVER_HOST = $ServerHost" -ForegroundColor White
    }
    
    if ($ServerPort -and $ServerPort -ne 8888) {
        [Environment]::SetEnvironmentVariable("PEEPERS_SERVER_PORT", $ServerPort.ToString(), "User")
        Write-Host "  PEEPERS_SERVER_PORT = $ServerPort" -ForegroundColor White
    }
    
    Write-Host "User environment variables set successfully!" -ForegroundColor Green
}

# Set system environment variables
function Set-SystemEnvironment {
    param($ApiUrl, $ServerHost, $ServerPort)
    
    if (-not (Test-Administrator)) {
        Write-Error "Administrator privileges required for system environment variables!"
        return
    }
    
    Write-Host "Setting system environment variables..." -ForegroundColor Green
    
    if ($ApiUrl) {
        [Environment]::SetEnvironmentVariable("PEEPERS_API_URL", $ApiUrl, "Machine")
        Write-Host "  PEEPERS_API_URL = $ApiUrl" -ForegroundColor White
    }
    
    if ($ServerHost -and $ServerHost -ne "0.0.0.0") {
        [Environment]::SetEnvironmentVariable("PEEPERS_SERVER_HOST", $ServerHost, "Machine")
        Write-Host "  PEEPERS_SERVER_HOST = $ServerHost" -ForegroundColor White
    }
    
    if ($ServerPort -and $ServerPort -ne 8888) {
        [Environment]::SetEnvironmentVariable("PEEPERS_SERVER_PORT", $ServerPort.ToString(), "Machine")
        Write-Host "  PEEPERS_SERVER_PORT = $ServerPort" -ForegroundColor White
    }
    
    Write-Host "System environment variables set successfully!" -ForegroundColor Green
}

# Set registry values
function Set-RegistryValues {
    param($ApiUrl, $ServerHost, $ServerPort)
    
    if (-not (Test-Administrator)) {
        Write-Error "Administrator privileges required for registry modifications!"
        return
    }
    
    Write-Host "Setting registry values..." -ForegroundColor Green
    
    $regPath = "HKLM:\SOFTWARE\PeepersDriver"
    
    # Create registry key if it doesn't exist
    if (-not (Test-Path $regPath)) {
        New-Item -Path $regPath -Force | Out-Null
    }
    
    if ($ApiUrl) {
        Set-ItemProperty -Path $regPath -Name "RemoteApiUrl" -Value $ApiUrl
        Write-Host "  RemoteApiUrl = $ApiUrl" -ForegroundColor White
    }
    
    if ($ServerHost -and $ServerHost -ne "0.0.0.0") {
        Set-ItemProperty -Path $regPath -Name "ServerHost" -Value $ServerHost
        Write-Host "  ServerHost = $ServerHost" -ForegroundColor White
    }
    
    if ($ServerPort -and $ServerPort -ne 8888) {
        Set-ItemProperty -Path $regPath -Name "ServerPort" -Value $ServerPort -PropertyType DWord
        Write-Host "  ServerPort = $ServerPort" -ForegroundColor White
    }
    
    Write-Host "Registry values set successfully!" -ForegroundColor Green
}

# View current configuration
function Show-Configuration {
    Write-Host "Current Configuration" -ForegroundColor Green
    Write-Host "====================" -ForegroundColor Green
    Write-Host ""
    
    Write-Host "Environment Variables:" -ForegroundColor Yellow
    Write-Host "  User Level:" -ForegroundColor Cyan
    Write-Host "    PEEPERS_API_URL = $([Environment]::GetEnvironmentVariable('PEEPERS_API_URL', 'User'))" -ForegroundColor White
    Write-Host "    PEEPERS_SERVER_HOST = $([Environment]::GetEnvironmentVariable('PEEPERS_SERVER_HOST', 'User'))" -ForegroundColor White
    Write-Host "    PEEPERS_SERVER_PORT = $([Environment]::GetEnvironmentVariable('PEEPERS_SERVER_PORT', 'User'))" -ForegroundColor White
    
    Write-Host "  System Level:" -ForegroundColor Cyan
    Write-Host "    PEEPERS_API_URL = $([Environment]::GetEnvironmentVariable('PEEPERS_API_URL', 'Machine'))" -ForegroundColor White
    Write-Host "    PEEPERS_SERVER_HOST = $([Environment]::GetEnvironmentVariable('PEEPERS_SERVER_HOST', 'Machine'))" -ForegroundColor White
    Write-Host "    PEEPERS_SERVER_PORT = $([Environment]::GetEnvironmentVariable('PEEPERS_SERVER_PORT', 'Machine'))" -ForegroundColor White
    
    Write-Host ""
    Write-Host "Registry Values:" -ForegroundColor Yellow
    $regPath = "HKLM:\SOFTWARE\PeepersDriver"
    if (Test-Path $regPath) {
        $regValues = Get-ItemProperty -Path $regPath -ErrorAction SilentlyContinue
        if ($regValues) {
            Write-Host "  RemoteApiUrl = $($regValues.RemoteApiUrl)" -ForegroundColor White
            Write-Host "  ServerHost = $($regValues.ServerHost)" -ForegroundColor White
            Write-Host "  ServerPort = $($regValues.ServerPort)" -ForegroundColor White
        } else {
            Write-Host "  No registry values found" -ForegroundColor Gray
        }
    } else {
        Write-Host "  Registry key not found" -ForegroundColor Gray
    }
    
    Write-Host ""
    Write-Host "Priority Order:" -ForegroundColor Yellow
    Write-Host "  1. Environment Variables (highest)" -ForegroundColor White
    Write-Host "  2. Registry Values" -ForegroundColor White
    Write-Host "  3. Default Values (lowest)" -ForegroundColor White
}

# Remove all configuration
function Remove-Configuration {
    Write-Host "Removing Peepers Driver Configuration..." -ForegroundColor Red
    
    # Remove user environment variables
    [Environment]::SetEnvironmentVariable("PEEPERS_API_URL", $null, "User")
    [Environment]::SetEnvironmentVariable("PEEPERS_SERVER_HOST", $null, "User")
    [Environment]::SetEnvironmentVariable("PEEPERS_SERVER_PORT", $null, "User")
    
    # Remove system environment variables (if admin)
    if (Test-Administrator) {
        [Environment]::SetEnvironmentVariable("PEEPERS_API_URL", $null, "Machine")
        [Environment]::SetEnvironmentVariable("PEEPERS_SERVER_HOST", $null, "Machine")
        [Environment]::SetEnvironmentVariable("PEEPERS_SERVER_PORT", $null, "Machine")
        
        # Remove registry key
        $regPath = "HKLM:\SOFTWARE\PeepersDriver"
        if (Test-Path $regPath) {
            Remove-Item -Path $regPath -Recurse -Force
        }
        
        Write-Host "All configuration removed successfully!" -ForegroundColor Green
    } else {
        Write-Host "User environment variables removed." -ForegroundColor Yellow
        Write-Host "Run as Administrator to remove system settings and registry values." -ForegroundColor Yellow
    }
}

# Interactive mode
function Start-InteractiveMode {
    Write-Host "Peepers Driver Configuration" -ForegroundColor Green
    Write-Host "============================" -ForegroundColor Green
    Write-Host ""
    
    if (-not (Test-Administrator)) {
        Write-Host "WARNING: Not running as Administrator. Some features will be limited." -ForegroundColor Yellow
        Write-Host ""
    }
    
    do {
        Write-Host "Choose an option:" -ForegroundColor Yellow
        Write-Host "1. Set User Environment Variables" -ForegroundColor White
        Write-Host "2. Set System Environment Variables (Admin required)" -ForegroundColor White
        Write-Host "3. Set Registry Values (Admin required)" -ForegroundColor White
        Write-Host "4. View Current Configuration" -ForegroundColor White
        Write-Host "5. Remove All Configuration" -ForegroundColor White
        Write-Host "6. Exit" -ForegroundColor White
        Write-Host ""
        
        $choice = Read-Host "Enter your choice (1-6)"
        
        switch ($choice) {
            "1" {
                $apiUrl = Read-Host "Enter Remote API URL"
                $serverHost = Read-Host "Enter Server Host (default: 0.0.0.0)"
                $serverPort = Read-Host "Enter Server Port (default: 8888)"
                
                if ([string]::IsNullOrEmpty($serverHost)) { $serverHost = "0.0.0.0" }
                if ([string]::IsNullOrEmpty($serverPort)) { $serverPort = "8888" }
                
                Set-UserEnvironment -ApiUrl $apiUrl -ServerHost $serverHost -ServerPort ([int]$serverPort)
            }
            "2" {
                $apiUrl = Read-Host "Enter Remote API URL"
                $serverHost = Read-Host "Enter Server Host (default: 0.0.0.0)"
                $serverPort = Read-Host "Enter Server Port (default: 8888)"
                
                if ([string]::IsNullOrEmpty($serverHost)) { $serverHost = "0.0.0.0" }
                if ([string]::IsNullOrEmpty($serverPort)) { $serverPort = "8888" }
                
                Set-SystemEnvironment -ApiUrl $apiUrl -ServerHost $serverHost -ServerPort ([int]$serverPort)
            }
            "3" {
                $apiUrl = Read-Host "Enter Remote API URL"
                $serverHost = Read-Host "Enter Server Host (default: 0.0.0.0)"
                $serverPort = Read-Host "Enter Server Port (default: 8888)"
                
                if ([string]::IsNullOrEmpty($serverHost)) { $serverHost = "0.0.0.0" }
                if ([string]::IsNullOrEmpty($serverPort)) { $serverPort = "8888" }
                
                Set-RegistryValues -ApiUrl $apiUrl -ServerHost $serverHost -ServerPort ([int]$serverPort)
            }
            "4" { Show-Configuration }
            "5" { 
                $confirm = Read-Host "Are you sure you want to remove all configuration? (y/N)"
                if ($confirm -eq "y" -or $confirm -eq "Y") {
                    Remove-Configuration
                }
            }
            "6" { return }
            default { Write-Host "Invalid choice. Please try again." -ForegroundColor Red }
        }
        
        Write-Host ""
        Read-Host "Press Enter to continue"
        Write-Host ""
    } while ($true)
}

# Main script logic
if ($Help) {
    Show-Help
    return
}

if ($View) {
    Show-Configuration
    return
}

if ($Remove) {
    $confirm = Read-Host "Are you sure you want to remove all configuration? (y/N)"
    if ($confirm -eq "y" -or $confirm -eq "Y") {
        Remove-Configuration
    }
    return
}

if ($SetUser) {
    Set-UserEnvironment -ApiUrl $ApiUrl -ServerHost $ServerHost -ServerPort $ServerPort
    return
}

if ($SetSystem) {
    Set-SystemEnvironment -ApiUrl $ApiUrl -ServerHost $ServerHost -ServerPort $ServerPort
    return
}

if ($SetRegistry) {
    Set-RegistryValues -ApiUrl $ApiUrl -ServerHost $ServerHost -ServerPort $ServerPort
    return
}

# If no parameters provided, start interactive mode
if (-not $PSBoundParameters.Count) {
    Start-InteractiveMode
}