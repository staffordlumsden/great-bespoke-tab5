#!/usr/bin/env python3
"""Back up the tested CYD and replace only its non-helper OTA app slot.

The factory Console, partition table, NVS/PHY data and clock-helper app are
validated and preserved. No erase command and no factory write are available.
"""
from pathlib import Path
import argparse,datetime,hashlib,importlib.util,json,os,re,subprocess,sys

ROOT=Path(__file__).resolve().parents[3]

def run(args,log):
    with log.open('w') as output:
        result=subprocess.run(args,stdout=output,stderr=subprocess.STDOUT)
    if result.returncode:raise RuntimeError(f'Device command failed; inspect private {log.name}')

def identity(text):
    matches=set(re.findall(r'MAC:\s*([0-9a-f:]{17})',text,re.I))
    if len(matches)!=1 or 'ESP32-D0WD-V3' not in text or 'revision v3.1' not in text:
        raise ValueError('Unexpected target identity')
    return next(iter(matches)).lower()

def describe(data,offset):
    raw=data[offset:offset+320]
    if len(raw)<320 or raw[0]!=0xe9 or int.from_bytes(raw[32:36],'little')!=0xabcd5432:return None
    return {'project':raw[80:112].split(b'\0',1)[0].decode('ascii'),
            'version':raw[48:80].split(b'\0',1)[0].decode('ascii')}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--image',type=Path,required=True)
    args=parser.parse_args()
    stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    private=ROOT/f'projects/usb-drive/artifacts/private/device-update-{stamp}'
    private.mkdir(parents=True);private.chmod(0o700)
    command=[sys.executable,'-m','esptool','--chip','esp32','--port',args.port,'--baud','115200']
    expected=identity((ROOT/'projects/launcher/artifacts/loader-migration-private/pre-migration-rom.log').read_text())
    run(command+['chip_id'],private/'identity.log')
    if identity((private/'identity.log').read_text())!=expected:raise ValueError('CYD MAC mismatch')
    backup=private/'full-flash-before.bin'
    run(command+['read_flash','0','0x400000',str(backup)],private/'backup.log')
    if identity((private/'backup.log').read_text())!=expected or backup.stat().st_size!=0x400000:
        raise ValueError('Incomplete or wrong-device backup')
    run(command+['verify_flash','0',str(backup)],private/'backup-verify.log')
    if identity((private/'backup-verify.log').read_text())!=expected:raise ValueError('CYD MAC mismatch')
    backup.chmod(0o600);data=backup.read_bytes()
    table_path=ROOT/'projects/launcher/firmware/build/partition_table/partition-table.bin'
    table=table_path.read_bytes()
    if data[0x8000:0x8000+len(table)]!=table:raise ValueError('Unexpected partition table')
    offsets=(0x10000,0x90000,0x240000);apps=[describe(data,x) for x in offsets]
    if not apps[0] or apps[0]['project']!='cyd_launcher':raise ValueError('Factory Console not present')
    helpers=[i for i,a in enumerate(apps[1:]) if a and a['project']=='CYDFileServer' and '-clock' in a['version']]
    if len(helpers)!=1:raise ValueError('Expected exactly one retained clock helper')
    helper_slot=helpers[0];slot=1-helper_slot;target_before=apps[1+slot]
    if not target_before:raise ValueError('Replaceable app slot is unreadable')
    spec=importlib.util.spec_from_file_location('package',ROOT/'projects/launcher/tools/package_sd_app.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    image=args.image.read_bytes();meta=module.inspect(image)
    if meta['project']!='USBDrive':raise ValueError('Not a USBDrive image')
    report={'backup_sha256':hashlib.sha256(data).hexdigest(),'console':apps[0],
            'apps':apps[1:],'target_slot':slot,'target_before':target_before,
            'helper_slot':helper_slot,'target_checked':True}
    (private/'device-before.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'TARGET_BACKUP_VERIFIED Console/NVS/table/helper preserved; replacing {target_before["project"]} slot={slot}',flush=True)
    frozen=command+['--after','no_reset'];retained=private/'retained-before.bin'
    run(frozen+['read_flash','0x8000','0x8000',str(retained)],private/'retained-before.log')
    if identity((private/'retained-before.log').read_text())!=expected or retained.read_bytes()[:len(table)]!=table:
        raise ValueError('Target/table changed')
    helper_offset=offsets[1+helper_slot];helper_header=private/'helper-current-header.bin'
    run(frozen+['read_flash',hex(helper_offset),'320',str(helper_header)],private/'helper-current.log')
    helper=describe(helper_header.read_bytes(),0)
    if not helper or helper['project']!='CYDFileServer' or '-clock' not in helper['version']:
        raise ValueError('Current clock helper changed')
    target_offset=offsets[1+slot]
    run(frozen+['write_flash','--flash_mode','dio','--flash_freq','40m','--flash_size','4MB',hex(target_offset),str(args.image)],private/'app-write.log')
    readback=private/'app-readback.bin'
    run(frozen+['read_flash',hex(target_offset),hex(len(image)),str(readback)],private/'app-readback.log')
    if readback.read_bytes()!=image:raise ValueError('App readback mismatch')
    retained_after=private/'retained-after.bin'
    run(frozen+['read_flash','0x8000','0x8000',str(retained_after)],private/'retained-after.log')
    if retained_after.read_bytes()!=retained.read_bytes():raise ValueError('Table/NVS/PHY changed during update')
    tool=ROOT/'.tools/esp-idf/components/app_update/otatool.py';env=os.environ.copy();env['IDF_PATH']=str(ROOT/'.tools/esp-idf')
    with (private/'select-slot.log').open('w') as output:
        result=subprocess.run([sys.executable,str(tool),'--port',args.port,'--baud','115200',
            '--partition-table-file',str(table_path),'switch_ota_partition','--slot',str(slot)],env=env,stdout=output,stderr=subprocess.STDOUT)
    if result.returncode:raise RuntimeError('Slot selection failed')
    outcome={'result':'PASS','slot':slot,'replaced':target_before,**meta,
             'retained_table_nvs_phy':'BYTE_MATCH','full_backup':str(backup)}
    (private/'update-result.json').write_text(json.dumps(outcome,indent=2)+'\n')
    print('DEVICE_UPDATE PASS app readback matches; Console/clock helper retained; table/NVS/PHY byte match',flush=True)

if __name__=='__main__':main()
