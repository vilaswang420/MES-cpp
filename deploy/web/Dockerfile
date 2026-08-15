# hms-web 管理后台镜像: node 构建 + nginx 托管静态 (见 docs/DEPLOY_LINUX.md)
# 生产由入口 nginx 统一代理 /api /ws, 前端只出静态文件
FROM node:20-alpine AS build
WORKDIR /src
COPY hms-web/package.json hms-web/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY hms-web/ .
RUN npm run build

FROM nginx:1.27-alpine
COPY --from=build /src/dist /usr/share/nginx/html
