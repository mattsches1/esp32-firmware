const gulp = require("gulp");

const run = require("gulp-run-command").default;

gulp.task("bundle-login-js", function () {
    const glob = require("glob");
    const browserify = require("browserify");
    const tsify = require("tsify");
    const fancy_log = require("fancy-log");
    const source = require("vinyl-source-stream");
    const buffer = require("vinyl-buffer");
    const uglify = require("gulp-uglify");

    var files = glob.sync('./src/**/*.ts');
    return browserify({ // Collect js dependencies
        basedir: ".",
        debug: false,
        entries: files,
        cache: {},
        packageCache: {},
    })
        .plugin(tsify, {noImplicitAny: true,
                        esModuleInterop: true,
                        target: "es5"}) // Compile typescript
        .bundle()
        .on("error", fancy_log)
        .pipe(source("login_bundle.js")) //Collect in bundle.js
        .pipe(buffer())
        .pipe(
            uglify()
        )
        .pipe(gulp.dest("assets/js")); // Write to assets/js/bundle.js(.map)
});

// Write minified html to assets/index.html
gulp.task("copy-login-html", function () {
    const htmlmin = require("gulp-html-minifier-terser");
    const inlineimg = require('gulp-inline-image-html');

    return gulp
        .src(["src/login.html"])
        .pipe(inlineimg('src'))
        .pipe(
            htmlmin({
                collapseBooleanAttributes: true,
                collapseInlineTagWhitespace: true,
                collapseWhitespace: true,
                conservativeCollapse: true,
                decodeEntities: true,
                includeAutoGeneratedTags: false,
                minifyCSS: true,
                minifyJS: true,
                minifyURLs: true,
                preventAttributesEscaping: true,
                processConditionalComments: true,
                removeAttributeQuotes: true,
                removeComments: true,
                removeEmptyAttributes: true,
                removeOptionalTags: true,
                removeRedundantAttributes: true,
                removeScriptTypeAttributes: true,
                removeStyleLinkTypeAttributes: true,
                sortAttributes: true,
                sortClassName: true,
                trimCustomFragments: true,
                useShortDoctype: true,
            })
        )
        .pipe(gulp.dest("assets"));
});

gulp.task("sass", function () {
    const { sass } = require("@mr-hope/gulp-sass");
    const purgecss = require("gulp-purgecss");
    const postcss = require("gulp-postcss");
    const autoprefixer = require("autoprefixer");
    const cssnano = require("cssnano");

    return gulp
        .src("src/scss/*.scss")
        .pipe(sass().on("error", sass.logError)) // Compile sass to css
        .pipe(purgecss({ //remove unused css
              content: ["src/*.html"],
              whitelistPatterns: [/shake/]
          }))
        .pipe(
            postcss([
                autoprefixer(), // Add browser-specific prefixes
                cssnano({ // Minify css
                    preset: "advanced",
                }),
            ])
        )
        .pipe(gulp.dest("assets/css")); // Write to assets/css/main.css
});

// Embed css and js into html
gulp.task("embed", run('"' + process.env.PYTHON_EXECUTABLE + '" embed_css_and_js.py'));

gulp.task("gzip", function () {
    const gzip = require('gulp-gzip');
    return gulp.src('dist/*.html')
        .pipe(gzip({ gzipOptions: { level: 9 } }))
        .pipe(gulp.dest('dist'));
});

gulp.task("generate_login_header", run('"' + process.env.PYTHON_EXECUTABLE + '" xxd.py dist/login.html.gz dist/login.html.h login_html_gz'));

gulp.task("default",
    gulp.series(
        "copy-login-html",
        "sass",
        "bundle-login-js",
        "embed",
        "gzip",
        "generate_login_header",
    )
);
