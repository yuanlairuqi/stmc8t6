$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
function Require([bool]$condition,[string]$message) {
    if(-not $condition) { throw $message }
}
function Read-Settings([string]$path) {
    $map=@{}
    foreach($line in Get-Content $path) {
        if($line -match '^([^#=]+)=(.*)$') {
            $map[$Matches[1].Replace('\ ',' ')]=$Matches[2]
        }
    }
    return $map
}
$settings=Read-Settings (Join-Path $projectRoot 'stmc8t6.ioc')
$before=Read-Settings (Join-Path $projectRoot 'backups/before_motor_20261013/stmc8t6.ioc')
foreach($key in $before.Keys | Where-Object { $_ -like 'RCC.*' -or $_ -like 'PA13.*' -or $_ -like 'PA14.*' }) {
    Require ($before[$key] -eq $settings[$key]) "Clock/SWD changed: $key"
}
$pinSignals=@{
    'PA8.Signal'='S_TIM1_CH1'; 'PB0.Signal'='GPIO_Output';
    'PA9.Signal'='USART1_TX'; 'PA10.Signal'='USART1_RX';
    'PA2.Signal'='USART2_TX'; 'PA3.Signal'='USART2_RX';
    'PB10.Signal'='USART3_TX'; 'PB11.Signal'='USART3_RX'
}
foreach($key in $pinSignals.Keys) { Require ($settings[$key] -eq $pinSignals[$key]) "Pin mismatch: $key" }
Require ($settings['TIM1.OCMode_PWM-PWM Generation1 CH1'] -eq 'TIM_OCMODE_PWM2') 'IOC must use PWM2'
Require ($settings['TIM1.Prescaler'] -eq '71') 'IOC PSC mismatch'
Require ($settings['TIM1.Period'] -eq '9999') 'IOC ARR mismatch'
Require ($settings['TIM1.Pulse-PWM Generation1 CH1'] -eq '5000') 'IOC CCR mismatch'
$main=[IO.File]::ReadAllText((Join-Path $projectRoot 'Core/Src/main.c'))
$original=[IO.File]::ReadAllText((Join-Path $projectRoot 'backups/before_motor_20261013/Core/Src/main.c'))
$clockPattern='void SystemClock_Config\(void\)\s*\{[\s\S]*?\n\}'
$clockNow=[regex]::Match($main,$clockPattern)
$clockBefore=[regex]::Match($original,$clockPattern)
Require ($clockNow.Success -and $clockBefore.Success) 'Clock function not found'
Require ($clockNow.Value -eq $clockBefore.Value) 'Generated clock function changed'
foreach($snippet in @('sConfigOC.OCMode = TIM_OCMODE_PWM2;','sConfigOC.Pulse = 5000;','htim1.Init.Prescaler = 71;','htim1.Init.Period = 9999;','Motor_StartOnce(&htim1);','Motor_Process();')) {
    Require ($main.Contains($snippet)) "Generated/user code missing: $snippet"
}
Require ([regex]::Matches($main,'Motor_StartOnce\(&htim1\);').Count -eq 1) 'Startup call must occur exactly once'
foreach($n in @(1,2,3)) {
    foreach($snippet in @("huart$n.Instance = USART$n;","huart$n.Init.BaudRate = 115200;","huart$n.Init.WordLength = UART_WORDLENGTH_8B;","huart$n.Init.StopBits = UART_STOPBITS_1;","huart$n.Init.Parity = UART_PARITY_NONE;","huart$n.Init.Mode = UART_MODE_TX_RX;","huart$n.Init.HwFlowCtl = UART_HWCONTROL_NONE;")) {
        Require ($main.Contains($snippet)) "UART default mismatch: $snippet"
    }
}
$cmake=[IO.File]::ReadAllText((Join-Path $projectRoot 'CMakeLists.txt'))
Require ($cmake.Contains('Core/Src/motor_motion.c')) 'Custom module missing from CMake target'
$config=[IO.File]::ReadAllText((Join-Path $projectRoot 'Core/Inc/motor_config.h'))
foreach($pair in @(@('MOTOR_TIMER_PRESCALER','TIM1.Prescaler'),@('MOTOR_TIMER_PERIOD','TIM1.Period'),@('MOTOR_TIMER_COMPARE','TIM1.Pulse-PWM Generation1 CH1'))) {
    $match=[regex]::Match($config,"#define\s+$($pair[0])\s+(\d+)U")
    Require $match.Success "Custom timer setting not found: $($pair[0])"
    Require ($match.Groups[1].Value -eq $settings[$pair[1]]) "Custom module / IOC mismatch: $($pair[0])"
}
$msp=[IO.File]::ReadAllText((Join-Path $projectRoot 'Core/Src/stm32f1xx_hal_msp.c'))
Require ($msp.Contains('__HAL_AFIO_REMAP_SWJ_NOJTAG();')) 'SWD preservation missing from MSP'
Require ($msp.Contains('__HAL_RCC_TIM1_CLK_ENABLE();')) 'TIM1 clock not enabled'
Require ($msp.Contains('GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;')) 'TIM/UART AF output mode missing'
foreach($uart in @(1,2,3)) {
    $pattern="if\(huart->Instance==USART$uart\)([\s\S]*?)/\* USER CODE END USART${uart}_MspInit 1 \*/"
    $block=[regex]::Match($msp,$pattern)
    Require $block.Success "USART$uart MSP init block missing"
    $port=if($uart -eq 3){'GPIOB'}else{'GPIOA'}
    $tx=if($uart -eq 1){9}elseif($uart -eq 2){2}else{10}
    $rx=$tx+1
    foreach($snippet in @("__HAL_RCC_USART${uart}_CLK_ENABLE();","GPIO_InitStruct.Pin = GPIO_PIN_$tx;","GPIO_InitStruct.Pin = GPIO_PIN_$rx;","HAL_GPIO_Init($port, &GPIO_InitStruct);",'GPIO_MODE_AF_PP','GPIO_MODE_INPUT')) {
        Require ($block.Value.Contains($snippet)) "USART$uart MSP binding mismatch: $snippet"
    }
}
$h=[IO.File]::ReadAllText((Join-Path $projectRoot 'Core/Inc/main.h'))
Require ($h.Contains('#define MOTOR_PUL_Pin GPIO_PIN_8') -and $h.Contains('#define MOTOR_PUL_GPIO_Port GPIOA')) 'PUL alias mismatch'
Require ($h.Contains('#define MOTOR_DIR_Pin GPIO_PIN_0') -and $h.Contains('#define MOTOR_DIR_GPIO_Port GPIOB')) 'DIR alias mismatch'
$halConfig=[IO.File]::ReadAllText((Join-Path $projectRoot 'Core/Inc/stm32f1xx_hal_conf.h'))
Require ($halConfig.Contains('#define HSE_VALUE    8000000U')) 'HSE_VALUE mismatch'
Require ($halConfig.Contains('#define HAL_TIM_MODULE_ENABLED') -and $halConfig.Contains('#define HAL_UART_MODULE_ENABLED')) 'Required HAL modules missing'
$appSources=Get-ChildItem (Join-Path $projectRoot 'Core/Src') -Filter '*.c' |
    ForEach-Object { [IO.File]::ReadAllText($_.FullName) }
Require (-not ($appSources -match 'HAL_UART_(Transmit|Receive)\s*\(')) 'Unrequested UART communication found'
Require (-not ($appSources -match 'HAL_TIM_PWM_Start\s*\(')) 'Uncontrolled PWM start found'
$pins=@($settings.Keys | Where-Object {$_ -match '^Mcu.Pin\d+$'} | ForEach-Object {$settings[$_]})
Require (($pins | Sort-Object -Unique).Count -eq $pins.Count) 'Duplicate pin in IOC'
$interrupts=[IO.File]::ReadAllText((Join-Path $projectRoot 'Core/Src/stm32f1xx_it.c'))
Require ($interrupts.Contains('HAL_IncTick();')) 'SysTick tick update missing'
Write-Output 'PASS IOC pin map, UART 115200/8N1, PWM2 PSC/ARR/CCR, original clock/SWD, one-shot user calls, CMake module'
Write-Output 'PASS custom timer constants match IOC, MSP clocks and GPIO bindings, pin aliases, HSE_VALUE, HAL modules, unique pins, no serial commands, SysTick'
