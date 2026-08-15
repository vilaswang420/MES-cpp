# hms-dashboard 大屏镜像: node 构建 + nginx 托管静态 (见 docs/DEPLOY_LINUX.md)
# 生产挂在子路径 /dashboard/ 下, 构建必须带 --base=/dashboard/
FROM node:20-alpine AS build
WORKDIR /src
COPY hms-dashboard/package.json hms-dashboard/package-lock.json* ./
RUN npm ci --no-audit --no-fund || npm install --no-audit --no-fund
COPY hms-dashboard/ .
RUN npx vite build --base=/dashboard/

FROM nginx:1.27-alpine
COPY --from=build /src/dist /usr/share/nginx/html
