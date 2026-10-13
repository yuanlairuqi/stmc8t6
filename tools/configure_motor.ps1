$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$backup = Join-Path $projectRoot 'backups/before_motor_20261013'
if (Test-Path $backup) { throw 'Backup already exists; do not overwrite original snapshot.' }
New-Item -ItemType Directory -Path $backup | Out-Null
foreach ($name in @('Core','cmake','stmc8t6.ioc','CMakeLists.txt','CMakePresets.json')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $backup -Recurse
}
$ioc = Join-Path $projectRoot 'stmc8t6.ioc'
$settings = [ordered]@{}
foreach ($line in Get-Content $ioc) {
    if ($line -match '^([^#=]+)=(.*)$') { $settings[$Matches[1]] = $Matches[2] }
}
foreach ($key in @($settings.Keys)) {
    if ($key -match '^Mcu\.(IP\d+|IPNb|Pin\d+|PinsNb)$') { $settings.Remove($key) }
}
$ips = @('NVIC','RCC','SYS','TIM1','USART1','USART2','USART3')
for ($i=0; $i -lt $ips.Count; $i++) { $settings["Mcu.IP$i"]=$ips[$i] }
$settings['Mcu.IPNb']=$ips.Count
$pins=@('PD0-OSC_IN','PD1-OSC_OUT','PA2','PA3','PA8','PA9','PA10','PA13','PA14','PB0','PB10','PB11','VP_SYS_VS_Systick','VP_TIM1_VS_ClockSourceINT')
for ($i=0; $i -lt $pins.Count; $i++) { $settings["Mcu.Pin$i"]=$pins[$i] }
$settings['Mcu.PinsNb']=$pins.Count
foreach ($entry in @(@('PA2','USART2_TX'),@('PA3','USART2_RX'),@('PA9','USART1_TX'),@('PA10','USART1_RX'),@('PB10','USART3_TX'),@('PB11','USART3_RX'))) {
    $settings["$($entry[0]).Mode"]='Asynchronous'
    $settings["$($entry[0]).Signal"]=$entry[1]
}
$settings['PA8.Signal']='S_TIM1_CH1'
$settings['PA8.GPIOParameters']='GPIO_Speed,GPIO_Label'
$settings['PA8.GPIO_Label']='MOTOR_PUL'
$settings['PA8.GPIO_Speed']='GPIO_SPEED_FREQ_MEDIUM'
$settings['PB0.Signal']='GPIO_Output'
$settings['PB0.GPIOParameters']='GPIO_Label'
$settings['PB0.GPIO_Label']='MOTOR_DIR'
$settings['SH.S_TIM1_CH1.0']='TIM1_CH1,PWM Generation1 CH1'
$settings['SH.S_TIM1_CH1.ConfNb']='1'
$settings['VP_TIM1_VS_ClockSourceINT.Mode']='Internal'
$settings['VP_TIM1_VS_ClockSourceINT.Signal']='TIM1_VS_ClockSourceINT'
$settings['TIM1.Channel-PWM Generation1 CH1']='TIM_CHANNEL_1'
$settings['TIM1.Prescaler']='71'
$settings['TIM1.Period']='9999'
$settings['TIM1.Pulse-PWM Generation1 CH1']='5000'
$settings['TIM1.OCMode_PWM-PWM Generation1 CH1']='TIM_OCMODE_PWM2'
$settings['TIM1.AutoReloadPreload']='TIM_AUTORELOAD_PRELOAD_ENABLE'
$settings['TIM1.RepetitionCounter']='0'
$settings['TIM1.IPParameters']='Channel-PWM Generation1 CH1,Prescaler,Period,Pulse-PWM Generation1 CH1,OCMode_PWM-PWM Generation1 CH1,AutoReloadPreload,RepetitionCounter'
foreach ($uart in @('USART1','USART2','USART3')) {
    $settings["$uart.VirtualMode"]='VM_ASYNC'
    $settings["$uart.BaudRate"]='115200'
    $settings["$uart.IPParameters"]='VirtualMode,BaudRate'
}
$settings['ProjectManager.functionlistsort']='1-SystemClock_Config-RCC-false-HAL-false,2-MX_GPIO_Init-GPIO-false-HAL-true,3-MX_TIM1_Init-TIM1-false-HAL-true,4-MX_USART1_UART_Init-USART1-false-HAL-true,5-MX_USART2_UART_Init-USART2-false-HAL-true,6-MX_USART3_UART_Init-USART3-false-HAL-true'
$lines=@('#MicroXplorer Configuration settings - do not modify')
$lines += $settings.Keys | Sort-Object | ForEach-Object { "$($_.Replace(' ', '\ '))=$($settings[$_])" }
[IO.File]::WriteAllText($ioc, ($lines -join "`r`n")+"`r`n", [Text.UTF8Encoding]::new($false))
$script = @("config load $ioc","project path $projectRoot",'project generate',"config save $ioc",'exit')
[IO.File]::WriteAllLines((Join-Path $PSScriptRoot 'generate_cube.txt'),$script,[Text.UTF8Encoding]::new($false))
