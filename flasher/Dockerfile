# Web flasher for ObCYDian: a static site served by nginx.
# Build context is this folder, so it works on its own (see compose.yaml).

FROM python:3.12-alpine AS build
ARG FIRMWARE_VERSION=latest
WORKDIR /src
COPY . flasher/
# Downloads the release's merged image and splits it into flash parts
RUN python flasher/build.py --out /site --version "$FIRMWARE_VERSION"

FROM nginx:1.27-alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /site /usr/share/nginx/html
EXPOSE 3008
HEALTHCHECK --interval=60s --timeout=5s CMD wget -q -O /dev/null http://127.0.0.1:3008/manifest.json || exit 1
