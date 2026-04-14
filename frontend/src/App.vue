<template>
  <router-view />
</template>

<style>
:root {
  color: #14213d;
  background: #f5f7fb;
  font-family: "Segoe UI", "PingFang SC", "Microsoft YaHei", sans-serif;
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  background:
    radial-gradient(circle at top left, rgba(56, 189, 248, 0.18), transparent 32%),
    radial-gradient(circle at right center, rgba(245, 158, 11, 0.12), transparent 28%),
    linear-gradient(180deg, #f8fbff 0%, #eef4fb 100%);
}

#app {
  min-height: 100vh;
}
</style>
