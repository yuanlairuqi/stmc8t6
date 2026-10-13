#!/usr/bin/env python3
"""Run production C/Java host tests without ESP-IDF, CubeMX or hardware."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
STM = ROOT / 'stmc8t6'
ESP = ROOT / 'esp32_robot'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default=shutil.which('gcc'))
    parser.add_argument('--java-bin', type=Path)
    args = parser.parse_args()
    if not args.cc:
        parser.error('GCC is required; pass --cc PATH')
    cc = str(Path(args.cc).resolve())
    env = os.environ.copy()
    env['PATH'] = str(Path(cc).parent) + os.pathsep + env.get('PATH', '')
    def java_tool(name):
        name += '.exe' if os.name == 'nt' else ''
        tool = str(args.java_bin / name) if args.java_bin else shutil.which(name)
        if not tool:
            parser.error('JDK is required; pass --java-bin PATH')
        return tool
    javac, java = java_tool('javac'), java_tool('java')
    results = ROOT / 'build' / 'host-all'
    results.mkdir(parents=True, exist_ok=True)
    groups = 0
    def run(command, cwd=ROOT):
        subprocess.run([str(x) for x in command], cwd=cwd, env=env, check=True)
    def test(command):
        nonlocal groups
        run(command)
        groups += 1
    def compile_c(name, directory, sources, includes, defines=()):
        output = results / (name + ('.exe' if os.name == 'nt' else ''))
        run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
             *['-D' + x for x in defines], *['-I' + str(x) for x in includes],
             *[directory / x for x in sources], '-o', output], directory)
        return output
    # The STM production files and ESP standalone peer must remain identical.
    pairs = [('shared/robot_wire.h', 'Core/Inc/robot_wire.h'),
             ('shared/robot_wire.c', 'Core/Src/robot_wire.c'),
             ('stm32_example/robot_peer.h', 'Core/Inc/robot_peer.h'),
             ('stm32_example/robot_peer.c', 'Core/Src/robot_peer.c')]
    for esp, stm in pairs:
        if (ESP / esp).read_bytes() != (STM / stm).read_bytes():
            raise RuntimeError('Shared protocol drift: ' + esp + ' / ' + stm)
    print('PASS: STM32/ESP32 shared protocol files match', flush=True)
    control = compile_c('esp_control', ESP, ['tests/test_control.c', 'main/robot_control.c'], [ESP / 'main'])
    status = results / 'status.bin'
    test([control, status])
    link = compile_c('esp_link', ESP, ['tests/test_link.c', 'main/robot_bridge.c', 'shared/robot_wire.c',
                                     'stm32_example/robot_peer.c'], [ESP / x for x in ['main', 'shared', 'stm32_example']])
    test([link])
    java_sources = [ESP / 'android/app/src/main/java/cn/robot/ble' / (x + '.java') for x in ['Protocol', 'CommandChannel']]
    java_sources += [ESP / 'android/tests' / (x + '.java') for x in ['ProtocolTest', 'CommandChannelTest']]
    run([javac, '-encoding', 'UTF-8', '-d', results, *java_sources])
    test([java, '-ea', '-cp', results, 'ProtocolTest', status])
    test([java, '-ea', '-cp', results, 'CommandChannelTest'])
    stm_includes = [STM / 'tests/stubs', STM / 'Core/Inc', ESP / 'main']
    for ppr in [7, 16, 200, 1600, 4080, 4096, 4112, 8176, 8192, 40000]:
        motor = compile_c('stm_motor_' + str(ppr), STM, ['tests/test_motor.c', 'Core/Src/motor_motion.c'],
                          stm_includes, ['MOTOR_HOST_TEST', 'MOTOR_DRIVER_PULSES_PER_REV=' + str(ppr)])
        test([motor])
    for scenario in ['bad-mode', 'unexpected-stop', 'null-timer', 'wrong-timer', 'inverted-polarity']:
        test([results / ('stm_motor_1600' + ('.exe' if os.name == 'nt' else '')), scenario])
    uart_sources = [STM / x for x in ['tests/test_robot_uart.c', 'Core/Src/motor_motion.c', 'Core/Src/robot_uart.c',
                                    'Core/Src/robot_wire.c', 'Core/Src/robot_peer.c']] + [ESP / 'main/robot_bridge.c']
    uart = compile_c('stm_uart', STM, uart_sources, stm_includes, ['MOTOR_HOST_TEST', 'ROBOT_UART_HOST_TEST'])
    for scenario in ['standalone', 'waiting-stop', 'running-stop', 'finished-batch-stop', 'rx-overflow', 'uart-error',
                     'rx-rearm-fail', 'bad-stream', 'new-session-move', 'tx-buffer', 'tx-timeout', 'tx-start-fail',
                     'link-timeout', 'duplicate', 'cross-bridge']:
        test([uart, scenario])
    shared = compile_c('stm_shared', STM, [ESP / 'tests/test_link.c', ESP / 'main/robot_bridge.c',
                                          STM / 'Core/Src/robot_wire.c', STM / 'Core/Src/robot_peer.c'],
                       [STM / 'Core/Inc', ESP / 'main'])
    test([shared])
    for enabled in [0, 1]:
        continuous = compile_c('stm_continuous_' + str(enabled), STM, ['tests/test_continuous.c', 'Core/Src/motor_motion.c'],
                               stm_includes, ['MOTOR_HOST_TEST', 'MOTOR_CONTINUOUS_ENABLED=' + str(enabled)])
        test([continuous])
    enabled_uart = compile_c('stm_uart_enabled', STM, uart_sources, stm_includes,
                             ['MOTOR_HOST_TEST', 'ROBOT_UART_HOST_TEST', 'MOTOR_CONTINUOUS_ENABLED=1'])
    test([enabled_uart, 'enabled-cycle'])
    print('PASS: ' + str(groups) + ' production host test groups; hardware not exercised', flush=True)

if __name__ == '__main__':
    main()
