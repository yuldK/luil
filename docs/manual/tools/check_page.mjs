// 문서 하나를 브라우저로 열어 본다: 콘솔 오류, 가로 넘침, 빈 코드 상자, 글에 새어 나온 null·undefined·NaN.
//
//   node docs/manual/tools/check_page.mjs docs/manual/lists.html [더 많은 문서…] [--shots 폴더]
//
// 데스크톱(1440)·휴대폰(390) 폭, 밝게·어둡게 네 가지로 연다. 글꼴과 highlight.js CDN에 닿지 못해도 검사는 돈다
// (highlight.js가 없으면 코드 상자는 강조 없이 그려진다). 문제가 있으면 1로 끝난다.
import { createRequire } from "module";
import path from "path";
import fs from "fs";

const require = createRequire(import.meta.url);
const { chromium } = require("playwright");

const args = process.argv.slice(2);
const shotsAt = args.indexOf("--shots");
const shots = shotsAt >= 0 ? args[shotsAt + 1] : null;
const files = args.filter((a, i) => !a.startsWith("--") && (shotsAt < 0 || i !== shotsAt + 1));
if (!files.length) {
  console.error("문서 경로를 하나 이상 준다.");
  process.exit(2);
}

const browser = await chromium.launch();
let bad = 0;
for (const file of files) {
  const abs = path.resolve(file);
  for (const [w, scheme] of [[1440, "light"], [390, "dark"], [390, "light"], [1440, "dark"]]) {
    const ctx = await browser.newContext({ viewport: { width: w, height: 900 }, colorScheme: scheme });
    const p = await ctx.newPage();
    await p.route(/fonts\.(googleapis|gstatic)\.com/, (r) => r.abort());
    const errs = [];
    p.on("pageerror", (e) => errs.push("pageerror: " + e.message));
    p.on("console", (m) => {
      if (m.type() === "error" && !/ERR_FAILED|ERR_TUNNEL|ERR_NAME|net::/.test(m.text())) errs.push("console: " + m.text());
    });
    await p.goto("file://" + abs, { waitUntil: "load" }).catch((e) => errs.push("goto: " + e.message));
    await p.waitForTimeout(700);
    const info = await p.evaluate(() => {
      const body = document.body.innerText;
      return {
        sw: document.documentElement.scrollWidth,
        cw: document.documentElement.clientWidth,
        emptyCode: [...document.querySelectorAll(".code")].filter((c) => !c.querySelector("pre") || !c.querySelector("pre").textContent.trim()).length,
        leaked: /\bundefined\b/.test(body) ? "undefined" : /(^|\s)null(\s|$)/.test(body) ? "null" : /\bNaN\b/.test(body) ? "NaN" : "",
        wide: [...document.querySelectorAll(".doc-body *")]
          .filter((el) => el.getBoundingClientRect().right > document.documentElement.clientWidth + 1 && !el.closest("pre, .table-wrap, .code, .tv, [data-allow-wide]"))
          .slice(0, 3)
          .map((el) => el.tagName.toLowerCase() + (el.className ? "." + String(el.className).split(" ")[0] : "")),
      };
    });
    const flags = [];
    if (info.sw > info.cw) flags.push(`가로 넘침 ${info.sw}px (${info.wide.join(", ")})`);
    if (info.emptyCode) flags.push(`빈 코드 상자 ${info.emptyCode}개 (fill_excerpts.py를 돌렸나?)`);
    if (info.leaked) flags.push(`글에 ${info.leaked}`);
    flags.push(...errs.slice(0, 5));
    console.log(`${path.basename(file).padEnd(22)} ${String(w).padEnd(5)} ${scheme.padEnd(6)} ${flags.length ? flags.join(" | ") : "ok"}`);
    if (flags.length) bad++;
    if (shots) {
      fs.mkdirSync(shots, { recursive: true });
      await p.screenshot({ path: path.join(shots, `${path.basename(file, ".html")}-${w}-${scheme}.png`) });
    }
    await ctx.close();
  }
}
await browser.close();
process.exit(bad ? 1 : 0);
