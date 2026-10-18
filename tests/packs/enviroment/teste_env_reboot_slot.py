#!/usr/bin/env python3
"""Tarefa 1 — isolamento do bug do slot daily-reboot na .24 (pack Enviroment).

POST /api/daily-reboot responde ok mas o GET devolve o valor antigo.
Usa slot 2 (slot 1 pode seguir turnos e confundir a leitura).
Estrategia de isolamento via campos de debug (cfg_mem, has_dr, dr_dump):
  - se dr_dump tem o valor novo mas stored_time reverteu -> escrita OK no doc,
    GET le de outro lado (dois docs / fallback NVS).
  - se dr_dump tambem reverteu -> o proprio doc perdeu (save/reload por cima).
Repõe SEMPRE os valores originais no fim (defaults repostos).
NUNCA mexe em wifi/email.
"""
import base64
import json
import sys
import time
import urllib.request
import urllib.error

BASE = "http://192.168.1.24"
AUTH = "Basic " + base64.b64encode(b"admin:admin").decode()
UA = {"User-Agent": "Joana-T1-RebootSlot/1.0", "Authorization": AUTH}
TEST_TIME = "04:44"  # valor distintivo, longe da hora atual; reposto logo a seguir


def req(path, method="GET", data=None, timeout=15):
    time.sleep(0.6)  # board faz rate-limit
    body = None
    headers = dict(UA)
    if data is not None:
        body = data.encode()
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    r = urllib.request.Request(BASE + path, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def get_slot(i):
    st, txt = req("/api/daily-reboot?i=%d" % i)
    if st != 200:
        return None
    return json.loads(txt)


def main():
    log = []
    # 1. baseline slots 1 e 2
    base1 = get_slot(1)
    base2 = get_slot(2)
    if base1 is None or base2 is None:
        print("FALHA: board sem resposta no GET (auth? offline?)")
        return 2
    log.append({"fase": "baseline_s1", "resp": base1})
    log.append({"fase": "baseline_s2", "resp": base2})

    # 2. POST slot 2 com valor de teste
    st, txt = req("/api/daily-reboot", method="POST",
                  data="i=2&time=%s&enabled=1" % TEST_TIME)
    log.append({"fase": "post_test", "http": st, "resp": txt})

    # 3. GET imediato + GET 5s depois (sem reboot pelo meio)
    after1 = get_slot(2)
    time.sleep(5)
    after2 = get_slot(2)
    log.append({"fase": "get_apos_post", "resp": after1})
    log.append({"fase": "get_apos_5s", "resp": after2})

    stuck = (after1 and after1.get("stored_time") == TEST_TIME
             and after1.get("enabled") is True)
    same5s = (after2 == after1)

    # 4. repor originais (slot 2)
    orig_t = base2.get("stored_time", "")
    orig_e = "1" if base2.get("enabled") else "0"
    st_r, txt_r = req("/api/daily-reboot", method="POST",
                      data="i=2&time=%s&enabled=%s" % (orig_t, orig_e))
    restored = get_slot(2)
    log.append({"fase": "restore", "http": st_r, "resp": txt_r, "get_final": restored})
    ok_restore = (restored and restored.get("stored_time") == base2.get("stored_time")
                  and restored.get("enabled") == base2.get("enabled"))

    print("=== T1 reboot-slot .24 ===")
    print("baseline s2: stored_time=%r enabled=%r has_dr=%r dr_dump=%r cfg_mem=%r" % (
        base2.get("stored_time"), base2.get("enabled"), base2.get("has_dr"),
        base2.get("dr_dump"), base2.get("cfg_mem")))
    print("POST teste: http=%s resp=%s" % (st, txt[:200]))
    if after1:
        print("GET apos POST: stored_time=%r enabled=%r has_dr=%r dr_dump=%r cfg_mem=%r" % (
            after1.get("stored_time"), after1.get("enabled"), after1.get("has_dr"),
            after1.get("dr_dump"), after1.get("cfg_mem")))
    print("GET apos 5s igual ao anterior: %s" % same5s)
    print("RESULTADO: %s" % ("PASSOU (gravou)" if stuck else "FALHOU (nao gravou)"))
    print("Restore originais: %s" % ("OK" if ok_restore else "ATENCAO: VERIFICAR"))
    if after1 and not stuck:
        if after1.get("dr_dump") and TEST_TIME in str(after1.get("dr_dump")):
            print("ISOLAMENTO: dr_dump TEM o valor novo mas stored_time reverteu -> "
                  "escrita OK no doc, GET le de outro lado (2 docs / fallback NVS cfg_drbt)")
        else:
            print("ISOLAMENTO: dr_dump NAO tem o valor novo -> o proprio doc perdeu "
                  "(save por cima / reload silencioso / set noutro doc)")

    with open(__file__.replace(".py", "_resultado.json"), "w") as f:
        json.dump(log, f, indent=1)
    return 0 if (stuck and ok_restore) else 1


if __name__ == "__main__":
    sys.exit(main())
