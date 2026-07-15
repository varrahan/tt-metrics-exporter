# syntax=docker/dockerfile:1.7
ARG BUILD_IMAGE=python:3.11.15-slim-bookworm@sha256:f5cf0344c9886ff24d34797578d5d7dd6e8911ae0fe5962bb55d0f89603ec361
ARG PYTHON_RUNTIME_IMAGE=gcr.io/distroless/python3-debian12@sha256:7d1042ce588ab97019fe95c24ffca7bc5a82ccdac572511d5e09bda4435c89c5
ARG VERSION=0.1.0

FROM ${BUILD_IMAGE} AS build
WORKDIR /src
COPY . .
ARG VERSION
ARG REVISION=unknown
ARG SOURCE_DATE_EPOCH=0
ENV SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}
RUN python3 -m pip install --require-hashes --no-deps \
      --requirement requirements-dev.lock \
    && grep -qx "__version__ = \"${VERSION}\"" \
      src/tt_metrics_exporter/_version.py \
    && scripts/ci/run_tests.py \
    && python3 -m ruff check src tests scripts \
    && python3 scripts/ci/check_docs.py \
    && python3 -m build --wheel --no-isolation --outdir /wheel \
    && mkdir -p /out/python \
    && python3 -m pip install --require-hashes --no-deps --no-compile \
         --requirement requirements.lock --target /out/python \
    && python3 -m zipfile -e /wheel/tt_metrics_exporter-*.whl /out/python \
    && mkdir -p /out/mounts/sysfs/class/tenstorrent \
                /out/mounts/sysfs/devices /out/mounts/allocations \
                /out/mounts/janitor /out/mounts/profiler

FROM scratch AS wheel
COPY --from=build /wheel /

FROM ${PYTHON_RUNTIME_IMAGE} AS runtime
ARG VERSION
ARG REVISION=unknown
ARG SOURCE_DATE_EPOCH=0
ARG SOURCE_URL=https://github.com/varrahan/tt-metrics-exporter
ARG BUILD_DATE=unknown
LABEL org.opencontainers.image.title="Tenstorrent Metrics Exporter" \
      org.opencontainers.image.description="Node-local Tenstorrent telemetry exporter" \
      org.opencontainers.image.source="${SOURCE_URL}" \
      org.opencontainers.image.revision="${REVISION}" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.created="${BUILD_DATE}" \
      org.opencontainers.image.licenses="Apache-2.0"
ENV PYTHONPATH=/app \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    TT_EXPORTER_REVISION=${REVISION} \
    TT_EXPORTER_BUILD_TIME=${SOURCE_DATE_EPOCH}
COPY --from=build /out/python /app
COPY --from=build /out/mounts /mnt/tt
COPY LICENSE /licenses/LICENSE
USER 65532:65532
EXPOSE 9400
ENTRYPOINT ["python3", "-m", "tt_metrics_exporter"]
CMD ["--sysfs-root", "/mnt/tt/sysfs", "--listen-address", "0.0.0.0", "--port", "9400", "--log-format", "json"]
