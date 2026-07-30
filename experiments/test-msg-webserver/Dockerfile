FROM denoland/deno:alpine-2.9.3

WORKDIR /app
COPY deno.json server.ts ./
COPY public ./public
RUN mkdir -p /app/data && chown -R deno:deno /app

USER deno
ENV PORT=8000
EXPOSE 8000
VOLUME ["/app/data"]
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
  CMD ["deno", "eval", "--allow-net", "const r=await fetch('http://127.0.0.1:8000/healthz');if(!r.ok)Deno.exit(1)"]
CMD ["task", "start"]
