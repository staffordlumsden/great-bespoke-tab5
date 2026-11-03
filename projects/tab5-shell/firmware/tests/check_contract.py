#!/usr/bin/env python3
"""Safety boundaries that cannot be exercised by a host filesystem test."""
from pathlib import Path
import hashlib
import re

firmware = Path(__file__).resolve().parents[1]
project = firmware.parent
repo = project.parents[1]
config = (firmware / 'sdkconfig').read_text()
for setting in ('CONFIG_IDF_TARGET="esp32p4"', 'CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y',
                'CONFIG_ESP32P4_REV_MIN_100=y', 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y',
                'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
                'CONFIG_SLAVE_IDF_TARGET_ESP32C6=y',
                'CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="esp32c6"',
                'CONFIG_IDF_SLAVE_TARGET="esp32c6"',
                'CONFIG_SLAVE_IDF_TARGET_ARCH_RISCV=y'):
    assert setting in config, setting
assert 'CONFIG_SLAVE_IDF_TARGET_ESP32C6=y' in (firmware / 'sdkconfig.defaults').read_text()
generated = firmware / 'build/config/sdkconfig.h'
if generated.exists():
    definitions = dict(re.findall(r'^#define (CONFIG_\w+) (.+)$', generated.read_text(), re.M))
    def expanded(name):
        seen = set()
        while name.startswith('CONFIG_'):
            assert name not in seen, 'cyclic generated configuration: ' + name
            seen.add(name)
            assert name in definitions, 'missing generated configuration: ' + name
            name = definitions[name].strip()
        return name
    for name, value in (('CONFIG_SLAVE_IDF_TARGET_ESP32C6', '1'),
                        ('CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET', '"esp32c6"'),
                        ('CONFIG_IDF_SLAVE_TARGET', '"esp32c6"')):
        assert expanded(name) == value, 'stale/wrong generated configuration: ' + name
lock = (firmware / 'dependencies.lock').read_text()
for name, version in (('esp_hosted', '1.4.0'), ('esp_wifi_remote', '0.8.5')):
    block = lock.split('espressif/' + name + ':', 1)[1].split('\n  espressif/', 1)[0]
    assert 'version: ' + version in block

def partition_rows(path):
    return [tuple(part.strip() for part in line.split(','))
            for line in path.read_text().splitlines() if line and not line.startswith('#')]

assert partition_rows(firmware / 'partitions.csv') == partition_rows(repo / 'projects/tab5-console/partitions.csv')
app = (firmware / 'main/app_main.cpp').read_text()
assert app.index('display.present_all()') < app.index('tab5_app_boot_ready(true)')
assert 'boot_deadline = milliseconds() + 15000' in app
assert 'nvs_flash_erase' not in app
network = (firmware / 'main/shell_network.cpp').read_text()
assert network.index('tab5_radio_power_set(true)') < network.index('esp_wifi_init(&init)')
assert 'WIFI_STORAGE_RAM' in network
transport = (firmware / 'main/ssh_connection.cpp').read_text()
assert transport.index('tofu_check_or_record(cfg.host') < transport.index('libssh2_userauth_publickey_frommemory(')
assert 'nvs_set_' not in transport and 'check_or_record(host, port' not in transport
assert 'known_host_matches(pins,request,fp)' in network
cmake = (firmware / 'CMakeLists.txt').read_text()
assert 'flash app-flash bootloader-flash partition-table-flash' in cmake
assert 'add_dependencies(${flash_target} tab5_shell_sd_only)' in cmake
lua = firmware / 'components/lua54'
for line in (lua / 'SOURCE_SHA256SUMS.txt').read_text().splitlines():
    digest, filename = line.split('  ', 1)
    assert hashlib.sha256((lua / filename).read_bytes()).hexdigest() == digest, filename
print('PASS Shell pre-v3/config/dependency/partition/first-frame/radio/security/source-provenance contracts')
