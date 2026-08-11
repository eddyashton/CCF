// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

(() => {
  "use strict";

  const slides = [...document.querySelectorAll(".slide")];
  const previousButton = document.querySelector("#previous");
  const nextButton = document.querySelector("#next");
  const overviewButton = document.querySelector("#overview");
  const notesButton = document.querySelector("#notes-toggle");
  const notesCloseButton = document.querySelector("#notes-close");
  const fullscreenButton = document.querySelector("#fullscreen");
  const currentNumber = document.querySelector("#current-number");
  const totalNumber = document.querySelector("#total-number");
  const progressBar = document.querySelector("#progress-bar");
  const speakerPanel = document.querySelector("#speaker-panel");
  const speakerText = document.querySelector("#speaker-text");
  const overviewPanel = document.querySelector("#overview-panel");
  const overviewGrid = document.querySelector("#overview-grid");
  const deck = document.querySelector(".deck");

  let currentIndex = 0;
  const clampIndex = (index) => Math.max(0, Math.min(slides.length - 1, index));

  const fitDeckToViewport = () => {
    const viewport = window.visualViewport;
    const viewportWidth =
      viewport?.width ?? document.documentElement.clientWidth;
    const viewportHeight =
      viewport?.height ?? document.documentElement.clientHeight;
    const railHeight =
      Number.parseFloat(
        getComputedStyle(document.documentElement).getPropertyValue(
          "--control-rail-height",
        ),
      ) || 0;
    const safeInset = 8;
    const scale = Math.min(
      (viewportWidth - safeInset * 2) / deck.offsetWidth,
      (viewportHeight - railHeight - safeInset * 2) / deck.offsetHeight,
    );

    document.documentElement.style.setProperty(
      "--deck-scale",
      String(Math.max(0.1, scale)),
    );
  };

  const updateSlide = (requestedIndex, updateHistory = true) => {
    currentIndex = clampIndex(requestedIndex);
    slides.forEach((slide, index) => {
      slide.classList.toggle("is-active", index === currentIndex);
      slide.setAttribute("aria-hidden", String(index !== currentIndex));
    });

    slides[currentIndex].querySelectorAll("pre").forEach((codePanel) => {
      codePanel.scrollTop = 0;
      codePanel.scrollLeft = 0;
    });

    currentNumber.textContent = String(currentIndex + 1);
    totalNumber.textContent = String(slides.length);
    progressBar.style.width = `${((currentIndex + 1) / slides.length) * 100}%`;
    previousButton.disabled = currentIndex === 0;
    nextButton.disabled = currentIndex === slides.length - 1;
    speakerText.textContent =
      slides[currentIndex].querySelector(".notes")?.textContent.trim() ?? "";
    document.title = `${slides[currentIndex].dataset.title} | CCF JS Internals`;

    document.querySelectorAll(".overview-item").forEach((item, index) => {
      item.classList.toggle("is-current", index === currentIndex);
    });

    if (updateHistory) {
      history.replaceState(null, "", `#${currentIndex + 1}`);
    }
  };

  const move = (delta) => updateSlide(currentIndex + delta);

  const toggleNotes = (force) => {
    const shouldOpen = force ?? !speakerPanel.classList.contains("is-open");
    speakerPanel.classList.toggle("is-open", shouldOpen);
    notesButton.setAttribute("aria-pressed", String(shouldOpen));
  };

  const toggleOverview = (force) => {
    const shouldOpen = force ?? !overviewPanel.classList.contains("is-open");
    overviewPanel.classList.toggle("is-open", shouldOpen);
    overviewPanel.setAttribute("aria-hidden", String(!shouldOpen));
    if (shouldOpen) {
      document.querySelectorAll(".overview-item")[currentIndex]?.focus();
    }
  };

  const toggleFullscreen = async () => {
    try {
      if (document.fullscreenElement) {
        await document.exitFullscreen();
      } else {
        await document.documentElement.requestFullscreen();
      }
    } catch (error) {
      console.warn("Fullscreen is unavailable", error);
    }
  };

  slides.forEach((slide, index) => {
    const item = document.createElement("button");
    item.type = "button";
    item.className = "overview-item";
    item.innerHTML = `<b>${String(index + 1).padStart(2, "0")}</b><span>${slide.dataset.title}</span>`;
    item.addEventListener("click", () => {
      updateSlide(index);
      toggleOverview(false);
    });
    overviewGrid.append(item);
  });

  previousButton.addEventListener("click", () => move(-1));
  nextButton.addEventListener("click", () => move(1));
  overviewButton.addEventListener("click", () => toggleOverview());
  notesButton.addEventListener("click", () => toggleNotes());
  notesCloseButton.addEventListener("click", () => toggleNotes(false));
  fullscreenButton.addEventListener("click", toggleFullscreen);
  window.addEventListener("resize", fitDeckToViewport);
  window.visualViewport?.addEventListener("resize", fitDeckToViewport);
  document.addEventListener("fullscreenchange", fitDeckToViewport);

  document.addEventListener("keydown", (event) => {
    if (
      event.target instanceof HTMLButtonElement &&
      (event.key === "Enter" || event.key === " ")
    ) {
      return;
    }
    if (
      event.key === "ArrowRight" ||
      event.key === "PageDown" ||
      event.key === " "
    ) {
      event.preventDefault();
      move(1);
    } else if (event.key === "ArrowLeft" || event.key === "PageUp") {
      event.preventDefault();
      move(-1);
    } else if (event.key === "Home") {
      updateSlide(0);
    } else if (event.key === "End") {
      updateSlide(slides.length - 1);
    } else if (
      event.key.toLowerCase() === "o" ||
      (event.key === "Escape" && overviewPanel.classList.contains("is-open"))
    ) {
      toggleOverview();
    } else if (event.key.toLowerCase() === "n") {
      toggleNotes();
    } else if (event.key.toLowerCase() === "f") {
      toggleFullscreen();
    }
  });

  window.addEventListener("hashchange", () => {
    const hashIndex = Number.parseInt(location.hash.slice(1), 10) - 1;
    if (Number.isInteger(hashIndex)) updateSlide(hashIndex, false);
  });

  const initialIndex = Number.parseInt(location.hash.slice(1), 10) - 1;
  updateSlide(Number.isInteger(initialIndex) ? initialIndex : 0, false);
  fitDeckToViewport();
})();
