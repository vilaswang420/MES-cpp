# mes-dashboard 大屏镜像: node 构建 + nginx 托管静态 (见 docs/DEPLOY_LINUX.md)
# 生产挂在子路径 /dashboard/ 下, 构建必须带 --base=/dashboard/
FROM node:20-alpine AS build
WORKDIR /src
COPY mes-dashboard/package.json mes-dashboard/package-lock.json* ./
RUN npm ci --no-audit --no-fund || npm install --no-audit --no-fund
COPY mes-dashboard/ .
RUN npx vite build --base=/dashboard/

FROM nginx:1.27-alpine
COPY --from=build /src/dist /usr/share/nginx/html
