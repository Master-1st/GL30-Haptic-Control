from pathlib import Path
import hashlib,json,re
root=Path(__file__).resolve().parents[2]
assets=root/'firmware-esp32/components/gl30_ui/assets'
metadata=json.loads((assets/'font-generation.json').read_text(encoding='utf-8'))
texts=''.join(re.findall(r'"(?:[^"\\]|\\.)*"',(root/'firmware-esp32/components/gl30_ui/src/gl30_render.c').read_text(encoding='utf-8')))
required={ord(c) for c in texts if '\u4e00'<=c<='\u9fff'}
# The 48 px body font is the complete fallback. The 64 px title font is an
# intentional subset because several glyphs from this compressed font are not
# valid at 64 px; render.c routes those labels to the body font instead.
body={int(c[2:],16) for c in metadata['fonts'][0]['codepoints']}
missing=required-body
assert not missing, ''.join(chr(c) for c in missing)
sources={'kk_oled':('f01831d63b1d426b629921edaba644732aa29223',['kk-oled-port','kk-oled-use','kk-oled-font']),
         'kk_ui':('582c3442ecbc539c1c82a342676b5b2eda69eee0',['kk-ui-port','kk-ui-use','kk-ui-extend'])}
manifest=[]
for repo,(commit,names) in sources.items():
    for name in names:
        source=root/'tmp/kk-upstream-20260914'/repo/'skills'/name
        target=root/'.agents/skills'/name
        count=0
        for file in source.rglob('*'):
            if not file.is_file(): continue
            copied=target/file.relative_to(source)
            assert file.read_bytes()==copied.read_bytes(), str(copied)
            count+=1
        manifest.append({'skill':name,'source':f'https://gitee.com/keysking/{repo}',
                         'commit':commit,'files_verified_identical':count,
                         'skill_sha256':hashlib.sha256((target/'SKILL.md').read_bytes()).hexdigest()})
(root/'.agents/skills/KK_UPSTREAM_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'skills':len(manifest),'files':sum(m['files_verified_identical'] for m in manifest),
                  'Chinese_glyphs_checked':len(required),'result':'PASS'}))
