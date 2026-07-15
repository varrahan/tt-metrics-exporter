#!/usr/bin/env python3
"""Assert exporter-specific Kubernetes manifest invariants."""

import re
import sys
from pathlib import Path

import yaml


def resource(documents, kind):
    matches = [document for document in documents if document.get("kind") == kind]
    assert len(matches) == 1, (kind, len(matches))
    return matches[0]


def main() -> None:
    manifest = Path(sys.argv[1])
    environment = sys.argv[2]
    documents = [doc for doc in yaml.safe_load_all(manifest.read_text()) if doc]
    account = resource(documents, "ServiceAccount")
    assert account["automountServiceAccountToken"] is False

    daemonset = resource(documents, "DaemonSet")
    pod = daemonset["spec"]["template"]["spec"]
    assert pod["automountServiceAccountToken"] is False
    assert pod["terminationGracePeriodSeconds"] > 10
    assert not pod.get("hostNetwork", False)
    assert not pod.get("hostPID", False)
    assert not pod.get("hostIPC", False)
    security = pod["securityContext"]
    assert security["runAsNonRoot"] is True
    assert security["runAsUser"] == 65532
    assert security["runAsGroup"] == 65532
    assert security["seccompProfile"]["type"] == "RuntimeDefault"
    assert "serviceAccountName" in pod
    assert "rbac.authorization.k8s.io" not in manifest.read_text()

    container = pod["containers"][0]
    implementation = daemonset["spec"]["template"]["metadata"]["labels"]["telemetry.tenstorrent.com/implementation"]
    assert implementation == "python"
    container_security = container["securityContext"]
    assert container_security["allowPrivilegeEscalation"] is False
    assert container_security["readOnlyRootFilesystem"] is True
    assert container_security["capabilities"]["drop"] == ["ALL"]
    assert not container_security.get("privileged", False)
    startup = container["startupProbe"]
    assert startup["httpGet"]["path"] == "/healthz"
    assert startup["periodSeconds"] * startup["failureThreshold"] >= 300
    assert container["livenessProbe"]["httpGet"]["path"] == "/healthz"
    assert container["readinessProbe"]["httpGet"]["path"] == "/readyz"
    assert all(mount["readOnly"] for mount in container["volumeMounts"])
    host_paths = [volume["hostPath"]["path"] for volume in pod["volumes"]]
    assert "/dev" not in host_paths
    assert not any("containerd.sock" in path or "docker.sock" in path for path in host_paths)

    service = resource(documents, "Service")
    assert service["spec"]["clusterIP"] == "None"
    assert service["spec"]["ports"][0]["name"] == "metrics"
    policy = resource(documents, "NetworkPolicy")
    assert policy["spec"]["policyTypes"] == ["Ingress"]
    assert policy["spec"]["ingress"][0]["ports"][0]["port"] == 9400

    args = container["args"]
    if environment == "production":
        assert "--require-device" in args
        assert "--collect-hwmon" in args
        assert {"--allocation-state-root", "--janitor-state-root", "--metalium-profiler-state-root"} <= set(args)
        assert pod["nodeSelector"]["tenstorrent.com/accelerator"] == "true"
        assert re.fullmatch(r".+@sha256:[0-9a-f]{64}", container["image"])
        assert not container["image"].endswith("0" * 64)
    elif environment == "ttsim":
        assert "--require-device" not in args
        assert "--collect-hwmon" not in args
        assert pod["nodeSelector"]["tenstorrent.com/ttsim"] == "true"


if __name__ == "__main__":
    main()
