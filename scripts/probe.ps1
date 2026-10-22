<#
.SYNOPSIS
Explores a running Thing server, invokes an action, or watches server-sent
events: for manual testing, with any server (the C++ one, the Rust
reference, an example).

.DESCRIPTION
Without -Invoke or -Watch, lists the server's Things (from /things/ and
their Thing Descriptions), reads every property, and counts invocations.
Redirects aren't followed, so what is shown is what the server answered.

.PARAMETER BaseUrl
The server, with its API prefix if it has one
(http://127.0.0.1:5000/api/v1). http://127.0.0.1:5000 by default.

.PARAMETER Thing
Only this Thing.

.PARAMETER Invoke
An action to invoke, as thing/action; the invocation is polled until it has
finished, and its record shown (its input and output as PowerShell re-reads
them; the overview shows values as the server wrote them). Exits 0 if it
completed.

.PARAMETER Body
The action's input, as JSON text (none by default).

.PARAMETER Watch
Server-sent events to watch until Ctrl-C: thing/property (a data property),
thing/event, thing/properties or thing/events.

.PARAMETER Timeout
How long to wait for an invocation, in seconds (60 by default).

.EXAMPLE
scripts\probe.ps1

.EXAMPLE
scripts\probe.ps1 -Invoke http_probe/slow -Body '{"seconds": 2}'

.EXAMPLE
scripts\probe.ps1 -Watch http_probe/value
#>
param(
    [string]$BaseUrl = 'http://127.0.0.1:5000',
    [string]$Thing,
    [string]$Invoke,
    [string]$Body,
    [string]$Watch,
    [int]$Timeout = 60
)

. (Join-Path $PSScriptRoot 'common.ps1')
$BaseUrl = $BaseUrl.TrimEnd('/')

Add-Type -AssemblyName System.Net.Http
$handler = New-Object System.Net.Http.HttpClientHandler
$handler.AllowAutoRedirect = $false
$client = New-Object System.Net.Http.HttpClient $handler

# Sends a request; returns its status, headers and body text, whatever the
# status.
function Send([string]$Method, [string]$Url, [string]$Json) {
    $request = New-Object System.Net.Http.HttpRequestMessage ([System.Net.Http.HttpMethod]::new($Method)), $Url
    if ($Json) {
        $request.Content = New-Object System.Net.Http.StringContent $Json, ([System.Text.Encoding]::UTF8), 'application/json'
    }
    try {
        $response = $client.SendAsync($request).GetAwaiter().GetResult()
    } catch {
        throw "Can't reach ${Url}: $($_.Exception.GetBaseException().Message)"
    }
    $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    return [pscustomobject]@{
        Status   = [int]$response.StatusCode
        Reason   = $response.ReasonPhrase
        Location = if ($response.Headers.Location) { $response.Headers.Location.ToString() } else { $null }
        Text     = $text
    }
}

function Shorten([string]$Text, [int]$Length = 100) {
    $Text = $Text -replace '\s+', ' '
    if ($Text.Length -le $Length) { return $Text }
    return $Text.Substring(0, $Length) + ' ...'
}

function Compact($Value) {
    if ($null -eq $Value) { return 'null' }
    return ConvertTo-Json -InputObject $Value -Compress -Depth 20
}

function Show-Status($Response) {
    $color = if ($Response.Status -lt 300) { 'Green' } elseif ($Response.Status -lt 400) { 'Yellow' } else { 'Red' }
    Write-Host "$($Response.Status) $($Response.Reason)" -ForegroundColor $color
}

# ---- Watching server-sent events ------------------------------------------------------

if ($Watch) {
    $url = "$BaseUrl/$Watch"
    Write-Host "Watching $url (Ctrl-C stops)" -ForegroundColor DarkGray
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if (-not $curl) { throw 'curl.exe is needed to watch a stream' }
    & $curl.Source -s -N -H 'Accept: text/event-stream' $url
    exit $LASTEXITCODE
}

# ---- Invoking an action ---------------------------------------------------------------

if ($Invoke) {
    $url = "$BaseUrl/$Invoke"
    Write-Host "POST $url $Body" -ForegroundColor Cyan
    $response = Send 'POST' $url $Body
    Show-Status $response
    if ($response.Status -in 200, 204) {
        # A synchronous action (the wot profile) answers with its output.
        Write-Host $response.Text
        exit 0
    }
    if ($response.Status -ne 201) {
        Write-Host $response.Text
        exit 1
    }
    $invocation = $response.Text | ConvertFrom-Json
    $href = if ($response.Location) { $response.Location } else { $invocation.href }
    Write-Host "location: $href" -ForegroundColor DarkGray
    $deadline = (Get-Date).AddSeconds($Timeout)
    do {
        Start-Sleep -Milliseconds 100
        $record = (Send 'GET' $href).Text | ConvertFrom-Json
        if ((Get-Date) -gt $deadline) { throw "The invocation didn't finish within $Timeout s" }
    } while ($record.status -in 'pending', 'running')
    $color = if ($record.status -eq 'completed') { 'Green' } else { 'Red' }
    Write-Host "status: $($record.status)" -ForegroundColor $color
    foreach ($field in 'input', 'output') {
        if ($record.PSObject.Properties.Name -contains $field) {
            Write-Host "${field}: $(Compact $record.$field)"
        }
    }
    if ($record.PSObject.Properties.Name -contains 'error' -and $null -ne $record.error) {
        Write-Host "error: $($record.error.title): $($record.error.detail)" -ForegroundColor Red
    }
    if ($record.PSObject.Properties.Name -contains 'log') {
        foreach ($entry in $record.log) {
            Write-Host "log: $($entry.levelname) $($entry.message)" -ForegroundColor DarkGray
        }
    }
    if ($record.status -eq 'completed') { exit 0 }
    exit 1
}

# ---- The overview ---------------------------------------------------------------------

$listing = Send 'GET' "$BaseUrl/things/"
if ($listing.Status -ne 200) {
    Write-Host "GET $BaseUrl/things/ -> $($listing.Status) $($listing.Reason): $(Shorten $listing.Text)" -ForegroundColor Red
    exit 1
}
$things = $listing.Text | ConvertFrom-Json
$names = @($things.PSObject.Properties | ForEach-Object { $_.Name })
$served = $names.Count
if ($Thing) {
    if ($names -notcontains $Thing) { throw "No Thing named $Thing (there are: $($names -join ', '))" }
    $names = @($Thing)
}
Write-Host "$BaseUrl serves $served Thing(s)" -ForegroundColor Cyan

foreach ($name in $names) {
    $td = (Send 'GET' $things.$name).Text | ConvertFrom-Json
    Write-Host ''
    Write-Host "$name ($($td.title))" -ForegroundColor Cyan -NoNewline
    if ($td.PSObject.Properties.Name -contains 'description') {
        Write-Host " - $(Shorten $td.description 80)" -NoNewline
    }
    Write-Host ''
    Write-Host "  $($things.$name)" -ForegroundColor DarkGray

    if ($td.PSObject.Properties.Name -contains 'properties') {
        Write-Host '  properties'
        foreach ($property in $td.properties.PSObject.Properties) {
            $value = Send 'GET' "$BaseUrl/$name/$($property.Name)"
            $notes = @()
            if ($property.Value.PSObject.Properties.Name -contains 'readOnly' -and $property.Value.readOnly) {
                $notes += 'read-only'
            }
            if ($property.Value.PSObject.Properties.Name -contains 'unit') { $notes += $property.Value.unit }
            $line = '    {0,-24} = {1}' -f $property.Name, (Shorten $value.Text 60)
            if ($value.Status -ne 200) {
                Write-Host ("$line  ($($value.Status) $($value.Reason))") -ForegroundColor Red
            } else {
                Write-Host $line -NoNewline
                if ($notes) { Write-Host "  ($($notes -join ', '))" -ForegroundColor DarkGray } else { Write-Host '' }
            }
        }
    }
    if ($td.PSObject.Properties.Name -contains 'actions') {
        Write-Host '  actions'
        foreach ($action in $td.actions.PSObject.Properties) {
            $parameters = ''
            if ($action.Value.PSObject.Properties.Name -contains 'input') {
                $schema = $action.Value.input
                if ($schema.PSObject.Properties.Name -contains 'properties') {
                    $parameterNames = @($schema.properties.PSObject.Properties | ForEach-Object { $_.Name })
                    $parameters = '(' + ($parameterNames -join ', ') + ')'
                } elseif ($schema.PSObject.Properties.Name -contains 'type') {
                    $parameters = "($($schema.type))"
                }
            }
            Write-Host "    $($action.Name)$parameters"
        }
    }
    if ($td.PSObject.Properties.Name -contains 'events') {
        Write-Host '  events'
        foreach ($affordance in $td.events.PSObject.Properties) {
            Write-Host "    $($affordance.Name)"
        }
    }
}

$invocations = Send 'GET' "$BaseUrl/action_invocations"
if ($invocations.Status -eq 200) {
    # Windows PowerShell's ConvertFrom-Json writes an array as one object.
    $parsed = $invocations.Text | ConvertFrom-Json
    $list = @($parsed)
    Write-Host ''
    Write-Host "$($list.Count) invocation(s) kept" -ForegroundColor Cyan
    foreach ($entry in $list) {
        Write-Host "  $($entry.status.PadRight(9)) $($entry.action)  $($entry.href)" -ForegroundColor DarkGray
    }
}
