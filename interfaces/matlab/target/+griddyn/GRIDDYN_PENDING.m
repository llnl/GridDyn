function v = GRIDDYN_PENDING()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 12);
  end
  v = vInitialized;
end
